////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/arangodb/arangodb/blob/devel/LICENSE
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "DatabaseTailingSyncer.h"
#include "ApplicationFeatures/ApplicationServer.h"
#include "Basics/Exceptions.h"
#include "Basics/ReadLocker.h"
#include "Basics/Result.h"
#include "Basics/ScopeGuard.h"
#include "Basics/StaticStrings.h"
#include "Basics/system-functions.h"
#include "Logger/Logger.h"
#include "Replication/DatabaseInitialSyncer.h"
#include "Replication/DatabaseReplicationApplier.h"
#include "Replication/ReplicationMetricsFeature.h"
#include "RestServer/DatabaseFeature.h"
#include "SimpleHttpClient/SimpleHttpClient.h"
#include "SimpleHttpClient/SimpleHttpResult.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "StorageEngine/StorageEngine.h"
#include "Transaction/Hints.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/voc-types.h"
#include "VocBase/vocbase.h"

#include <absl/strings/str_cat.h>
#include <velocypack/Builder.h>
#include <velocypack/Iterator.h>
#include <velocypack/Parser.h>
#include <velocypack/Slice.h>

using namespace arangodb;
using namespace arangodb::basics;
using namespace arangodb::httpclient;
using namespace arangodb::rest;

namespace {
constexpr std::string_view cuidRef("cuid");
}

DatabaseTailingSyncer::DatabaseTailingSyncer(
    TRI_vocbase_t& vocbase,
    ReplicationApplierConfiguration const& configuration,
    TRI_voc_tick_t initialTick, bool useTick)
    : TailingSyncer(vocbase.replicationApplier(), configuration, initialTick,
                    useTick),
      _vocbase(&vocbase),
      _toTick(0),
      _lastCancellationCheck(std::chrono::steady_clock::now()),
      _queriedTranslations(false),
      _unregisteredFromLeader(false) {
  _state.vocbases.try_emplace(vocbase.name(), vocbase);

  if (configuration._database.empty()) {
    _state.databaseName = vocbase.name();
  }
}

std::shared_ptr<DatabaseTailingSyncer> DatabaseTailingSyncer::create(
    TRI_vocbase_t& vocbase,
    ReplicationApplierConfiguration const& configuration,
    TRI_voc_tick_t initialTick, bool useTick) {
  // enable make_shared on a class with a private constructor
  struct Enabler final : public DatabaseTailingSyncer {
    Enabler(TRI_vocbase_t& vocbase,
            ReplicationApplierConfiguration const& configuration,
            TRI_voc_tick_t initialTick, bool useTick)
        : DatabaseTailingSyncer(vocbase, configuration, initialTick, useTick) {}
  };

  return std::make_shared<Enabler>(vocbase, configuration, initialTick,
                                   useTick);
}

/// @brief save the current applier state
Result DatabaseTailingSyncer::saveApplierState() {
  auto rv = _applier->persistStateResult(false);
  if (rv.fail()) {
    THROW_ARANGO_EXCEPTION(rv);
  }
  return rv;
}

Result DatabaseTailingSyncer::syncCollectionCatchup(
    std::string const& collectionName, TRI_voc_tick_t fromTick, double timeout,
    TRI_voc_tick_t& until, bool& didTimeout, std::string const& context) {
  TRI_ASSERT(ServerState::instance()->isDBServer());
  TRI_ASSERT(!_unregisteredFromLeader);

  try {
    // always start from _initialTick
    _initialTick = fromTick;
    Result res = syncCollectionCatchupInternal(collectionName, timeout, false,
                                               until, didTimeout, context);
    if (res.fail()) {
      // if we failed, we can already unregister ourselves on the leader, so
      // that we don't block WAL pruning
      unregisterFromLeader(false);
    }
    _stats.publish();
    return res;
  } catch (std::exception const& ex) {
    // when we leave this method, we must unregister ourselves from the leader,
    // otherwise the leader may keep WAL logs around for us for too long
    unregisterFromLeader(false);
    _stats.publish();
    return {TRI_ERROR_INTERNAL, ex.what()};
  }
}

/// @brief finalize the synchronization of a collection by tailing the WAL
/// and filtering on the collection name until no more data is available
Result DatabaseTailingSyncer::syncCollectionFinalize(
    std::string const& collectionName, TRI_voc_tick_t fromTick,
    TRI_voc_tick_t toTick, std::string const& context) {
  TRI_ASSERT(ServerState::instance()->isDBServer());
  TRI_ASSERT(!_unregisteredFromLeader);

  try {
    // always start from _initialTick
    _initialTick = fromTick;
    _toTick = 0;
    if (toTick > 0) {
      _toTick = toTick;
    }

    // timeouts will be ignored by syncCollectionCatchupInternal if we set
    // "hard" to true.
    TRI_voc_tick_t dummy = 0;
    bool dummyDidTimeout = false;
    double dummyTimeout = 300.0;
    Result res = syncCollectionCatchupInternal(collectionName, dummyTimeout,
                                               /*hard*/ true, dummy,
                                               dummyDidTimeout, context);

    if (res.ok()) {
      // now do a final sync-to-disk call. note that this can fail
      auto& engine = vocbase()->engine();
      res =
          engine.flushWal(/*waitForSync*/ true, /*flushColumnFamilies*/ false);
    }

    if (res.fail()) {
      LOG_TOPIC("53048", DEBUG, Logger::REPLICATION)
          << "syncCollectionFinalize failed for collection '" << collectionName
          << "': " << res.errorMessage();
    }

    // always unregister our tailer, because syncCollectionFinalize is at
    // the end of the sync progress
    unregisterFromLeader(true);

    return res;
  } catch (std::exception const& ex) {
    // when we leave this method, we must unregister ourselves from the leader,
    // otherwise the leader may keep WAL logs around for us for too long
    unregisterFromLeader(true);
    return {TRI_ERROR_INTERNAL, ex.what()};
  }
}

Result DatabaseTailingSyncer::inheritFromInitialSyncer(
    DatabaseInitialSyncer const& syncer) {
  replutils::LeaderInfo const& leaderInfo = syncer.leaderInfo();

  TRI_ASSERT(!leaderInfo.endpoint.empty());
  TRI_ASSERT(leaderInfo.endpoint == _state.leader.endpoint);
  TRI_ASSERT(leaderInfo.serverId.isSet());
  TRI_ASSERT(!leaderInfo.engine.empty());
  TRI_ASSERT(leaderInfo.version() > 0);

  _state.leader.serverId = leaderInfo.serverId;
  _state.leader.engine = leaderInfo.engine;
  _state.leader.majorVersion = leaderInfo.majorVersion;
  _state.leader.minorVersion = leaderInfo.minorVersion;

  _initialTick = syncer.getLastLogTick();

  return registerOnLeader();
}

Result DatabaseTailingSyncer::registerOnLeader() {
  std::string const url =
      absl::StrCat(tailingBaseUrl("tail"), "chunkSize=1024&from=", _initialTick,
                   "&trackOnly=true&serverId=", _state.localServerIdString,
                   "&syncerId=", syncerId().toString());
  LOG_TOPIC("41510", DEBUG, Logger::REPLICATION)
      << "registering tailing syncer on leader, url: " << url;

  std::unique_ptr<httpclient::SimpleHttpResult> response;
  // register ourselves on leader once - using a small WAL tail attempt
  _state.connection.lease([&](httpclient::SimpleHttpClient* client) {
    // simply send the request, but don't care about the response. if it
    // fails, there is not much we can do from here.
    response.reset(client->request(rest::RequestType::GET, url, nullptr, 0));
  });

  if (replutils::hasFailed(response.get())) {
    return replutils::buildHttpError(response.get(), url, _state.connection);
  }
  return {};
}

void DatabaseTailingSyncer::unregisterFromLeader(bool hard) {
  if (!_unregisteredFromLeader) {
    try {
      _state.connection.lease([&](httpclient::SimpleHttpClient* client) {
        std::unique_ptr<httpclient::SimpleHttpResult> response;
        std::string url = absl::StrCat(tailingBaseUrl("tail"),
                                       "serverId=", _state.localServerIdString,
                                       "&syncerId=", syncerId().toString());
        LOG_TOPIC("22640", DEBUG, Logger::REPLICATION)
            << "unregistering tailing syncer from leader, url: " << url;

        if (hard) {
          url += "&withHardLock=true";
        }

        // simply send the request, but don't care about the response. if it
        // fails, there is not much we can do from here.
        response.reset(
            client->request(rest::RequestType::DELETE_REQ, url, nullptr, 0));
        _unregisteredFromLeader = true;
      });
    } catch (...) {
      // this must be exception-safe, but if an exception occurs, there is not
      // much we can do
    }
  }
}

/// @brief order a new chunk from the /tail API
void DatabaseTailingSyncer::fetchWalChunk(
    std::shared_ptr<Syncer::JobSynchronizer> sharedStatus,
    std::string_view baseUrl, std::string_view collectionName,
    TRI_voc_tick_t fromTick, TRI_voc_tick_t lastScannedTick) {
  if (vocbase()->server().isStopping()) {
    sharedStatus->gotResponse(Result(TRI_ERROR_SHUTTING_DOWN));
    return;
  }

  try {
    // assemble URL to call
    std::string url = absl::StrCat(baseUrl, "&from=", fromTick,
                                   "&lastScanned=", lastScannedTick);

    LOG_TOPIC("066a8", DEBUG, Logger::REPLICATION)
        << "tailing WAL for collection '" << collectionName
        << "', url: " << url;

    double t = TRI_microtime();

    // send request
    std::unique_ptr<httpclient::SimpleHttpResult> response;
    _state.connection.lease([&](httpclient::SimpleHttpClient* client) {
      auto headers = replutils::createHeaders();
      response.reset(client->retryRequest(rest::RequestType::GET, url, nullptr,
                                          0, headers));
    });

    t = TRI_microtime() - t;

    if (replutils::hasFailed(response.get())) {
      sharedStatus->gotResponse(
          replutils::buildHttpError(response.get(), url, _state.connection), t);
      return;
    }

    // success!
    sharedStatus->gotResponse(std::move(response), t);
  } catch (basics::Exception const& ex) {
    sharedStatus->gotResponse(Result(ex.code(), ex.what()));
  } catch (std::exception const& ex) {
    sharedStatus->gotResponse(Result(TRI_ERROR_INTERNAL, ex.what()));
  }
}

/// @brief finalize the synchronization of a collection by tailing the WAL
/// and filtering on the collection name until no more data is available
Result DatabaseTailingSyncer::syncCollectionCatchupInternal(
    std::string const& collectionName, double timeout, bool hard,
    TRI_voc_tick_t& until, bool& didTimeout, std::string const& context) {
  didTimeout = false;

  setAborted(false);

  TRI_ASSERT(!_state.isChildSyncer);
  TRI_ASSERT(!_state.leader.endpoint.empty());

  Result r;

  if (_state.leader.engine.empty()) {
    // fetch leader state only if we need to. this should not be needed,
    // normally
    TRI_ASSERT(false);

    r = _state.leader.getState(_state.connection,
                               /*_state.isChildSyncer*/ false, context.c_str());
    if (r.fail()) {
      return r;
    }
  } else {
    LOG_TOPIC("6c922", DEBUG, arangodb::Logger::REPLICATION)
        << "connected to leader at " << _state.leader.endpoint << ", version "
        << _state.leader.majorVersion << "." << _state.leader.minorVersion
        << ", context: " << context;
  }

  TRI_ASSERT(_state.leader.serverId.isSet());
  TRI_ASSERT(!_state.leader.engine.empty());
  TRI_ASSERT(_state.leader.version() > 0);

  // print extra info for debugging
  _state.applier._verbose = true;
  // we do not want to apply rename, create and drop collection operations
  _ignoreRenameCreateDrop = true;

  TRI_voc_tick_t fromTick = _initialTick;
  TRI_voc_tick_t lastScannedTick = fromTick;

  if (hard) {
    LOG_TOPIC("0e15c", DEBUG, Logger::REPLICATION)
        << "starting syncCollectionFinalize: " << collectionName
        << ", fromTick " << fromTick
        << ", toTick: " << (_toTick > 0 ? std::to_string(_toTick) : "");
  } else {
    LOG_TOPIC("70711", DEBUG, Logger::REPLICATION)
        << "starting syncCollectionCatchup: " << collectionName << ", fromTick "
        << fromTick;
  }

  double t = TRI_microtime();

  VPackBuilder builder;  // will be recycled for every batch

  auto clock = std::chrono::steady_clock();
  auto startTime = clock.now();

  std::string baseUrl =
      absl::StrCat(tailingBaseUrl("tail"),
                   "collection=", StringUtils::urlEncode(collectionName),
                   "&chunkSize=", _state.applier._chunkSize,
                   "&serverId=", _state.localServerIdString);

  if (syncerId().value > 0) {
    // we must only send the syncerId along if it is != 0, otherwise we will
    // trigger an error on the leader
    baseUrl += "&syncerId=" + syncerId().toString();
  }
  if (hard) {
    baseUrl += "&withHardLock=true";
  }

  // optional upper bound for tailing (used to stop tailing if we have the
  // exclusive lock on the leader and can be sure that no writes can happen on
  // the leader)
  if (_toTick > 0) {
    baseUrl += "&to=" + StringUtils::itoa(_toTick);
  }

  // the shared status will wait in its destructor until all posted
  // requests have been completed/canceled!
  auto self = shared_from_this();
  Syncer::JobSynchronizerScope sharedStatus(self);

  // order initial chunk. this will block until the initial response
  // has arrived
  fetchWalChunk(sharedStatus.clone(), baseUrl, collectionName, fromTick,
                lastScannedTick);

  while (true) {
    if (_checkCancellation) {
      // execute custom check for abortion only every few seconds, in case
      // it is expensive
      constexpr auto checkFrequency = std::chrono::seconds(5);

      auto now = std::chrono::steady_clock::now();
      TRI_IF_FAILURE("Replication::forceCheckCancellation") {
        // always force the cancellation check!
        _lastCancellationCheck = now - checkFrequency;
      }

      if (now - _lastCancellationCheck >= checkFrequency) {
        _lastCancellationCheck = now;
        if (_checkCancellation()) {
          return Result(
              TRI_ERROR_REPLICATION_SHARD_SYNC_ATTEMPT_TIMEOUT_EXCEEDED);
        }
      }
    }

    std::unique_ptr<httpclient::SimpleHttpResult> response;

    // block until we either got a response or were shut down
    r = sharedStatus->waitForResponse(response);

    ++_stats.numTailingRequests;
    _stats.waitedForTailing += sharedStatus->time();

    if (r.fail()) {
      until = fromTick;
      // no response or error or shutdown
      return r;
    }

    if (response->getHttpReturnCode() == 204) {
      // HTTP 204 No content: this means we are done
      TRI_ASSERT(r.ok());
      until = fromTick;
      return r;
    }

    if (response->hasContentLength()) {
      _stats.numTailingBytesReceived += response->getContentLength();
    }

    bool found;
    std::string header = response->getHeaderField(
        StaticStrings::ReplicationHeaderCheckMore, found);
    bool checkMore = false;
    if (found) {
      checkMore = StringUtils::boolean(header);
    }

    header = response->getHeaderField(
        StaticStrings::ReplicationHeaderLastScanned, found);
    if (found) {
      lastScannedTick = StringUtils::uint64(header);
    }

    header = response->getHeaderField(
        StaticStrings::ReplicationHeaderLastIncluded, found);
    if (!found) {
      until = fromTick;
      return Result(TRI_ERROR_REPLICATION_INVALID_RESPONSE,
                    absl::StrCat("got invalid response from leader at ",
                                 _state.leader.endpoint, ": required header ",
                                 StaticStrings::ReplicationHeaderLastIncluded,
                                 " is missing"));
    }
    TRI_voc_tick_t lastIncludedTick = StringUtils::uint64(header);

    // was the specified from value included the result?
    bool fromIncluded = false;
    header = response->getHeaderField(
        StaticStrings::ReplicationHeaderFromPresent, found);
    if (found) {
      fromIncluded = StringUtils::boolean(header);
    }
    if (!fromIncluded && fromTick > 0) {
      until = fromTick;
      abortOngoingTransactions();
      ++_stats.numFollowTickNotPresent;
      return Result(
          TRI_ERROR_REPLICATION_START_TICK_NOT_PRESENT,
          absl::StrCat(
              "required follow tick value '", lastIncludedTick,
              "' is not present (anymore?) on leader at ",
              _state.leader.endpoint, ". Last tick available on leader is '",
              lastIncludedTick,
              "'. It may be required to do a full resync and increase the "
              "number of historic logfiles on the leader."));
    }

    TRI_voc_tick_t oldFromTick = fromTick;

    // update the tick from which we will fetch in the next round
    if (lastIncludedTick > fromTick) {
      fromTick = lastIncludedTick;
    } else if (lastIncludedTick == 0 && lastScannedTick > 0 &&
               lastScannedTick > fromTick) {
      fromTick = lastScannedTick - 1;
    } else if (checkMore) {
      // we got the same tick again, this indicates we're at the end
      checkMore = false;
      LOG_TOPIC("098be", WARN, Logger::REPLICATION)
          << "we got the same tick again, "
          << "this indicates we're at the end";
    }

    if (checkMore) {
      // already fetch next batch in the background, by posting the
      // request to the scheduler, which can run it asynchronously
      sharedStatus->request([self, baseUrl, sharedStatus = sharedStatus.clone(),
                             collectionName, fromTick, lastScannedTick]() {
        std::static_pointer_cast<DatabaseTailingSyncer>(self)->fetchWalChunk(
            sharedStatus, baseUrl, collectionName, fromTick, lastScannedTick);
      });
    }

    builder.clear();

    ApplyStats applyStats;
    uint64_t ignoreCount = 0;
    r = applyLog(response.get(), oldFromTick, applyStats, builder, ignoreCount);
    if (r.fail()) {
      until = fromTick;
      return r;
    }

    // If this is non-hard, we employ some heuristics to stop early:
    if (!hard) {
      if (clock.now() - startTime > std::chrono::duration<double>(timeout) &&
          _ongoingTransactions.empty()) {
        checkMore = false;
        didTimeout = true;
      } else {
        TRI_voc_tick_t lastTick = 0;
        header = response->getHeaderField(
            StaticStrings::ReplicationHeaderLastTick, found);
        if (found) {
          lastTick = StringUtils::uint64(header);
          if (_ongoingTransactions.empty() &&
              lastTick > lastIncludedTick &&  // just to make sure!
              lastTick - lastIncludedTick < 1000) {
            checkMore = false;
          }
        }
      }
    }

    if (!checkMore) {
      // done!
      TRI_ASSERT(r.ok());
      until = fromTick;

      LOG_TOPIC("942ff", DEBUG, Logger::REPLICATION)
          << "finished syncCollection" << (hard ? "Finalize" : "Catchup")
          << ": " << collectionName << ", initialTick " << _initialTick
          << ", last fromTick: " << fromTick
          << ", toTick: " << (_toTick > 0 ? std::to_string(_toTick) : "")
          << ", tailing requests: " << _stats.numTailingRequests
          << ", waited for tailing: " << _stats.waitedForTailing
          << "s, total catchup time: " << (TRI_microtime() - t) << "s";

      return r;
    }

    LOG_TOPIC("2598f", DEBUG, Logger::REPLICATION)
        << "Fetching more data, fromTick: " << fromTick
        << ", lastScannedTick: " << lastScannedTick;
  }
}

bool DatabaseTailingSyncer::skipMarker(VPackSlice slice) {
  // we do not have a "cname" attribute in the marker...
  // now check for a globally unique id attribute ("cuid")
  // if its present, then we will use our local cuid -> collection name
  // translation table
  VPackSlice name = slice.get(::cuidRef);
  if (!name.isString()) {
    return false;
  }

  if (!_queriedTranslations) {
    // no translations yet... query leader inventory to find names of all
    // collections
    try {
      VPackBuilder inventoryResponse;

      auto syncer = DatabaseInitialSyncer::create(*_vocbase, _state.applier);
      Result res = syncer->getInventory(inventoryResponse);
      _queriedTranslations = true;
      if (res.fail()) {
        LOG_TOPIC("89080", ERR, Logger::REPLICATION)
            << "got error while fetching leader inventory for collection name "
               "translations: "
            << res.errorMessage();
        return false;
      }
      VPackSlice invSlice = inventoryResponse.slice();
      if (!invSlice.isObject()) {
        return false;
      }
      invSlice = invSlice.get("collections");
      if (!invSlice.isArray()) {
        return false;
      }

      for (VPackSlice it : VPackArrayIterator(invSlice)) {
        if (!it.isObject()) {
          continue;
        }
        VPackSlice c = it.get("parameters");
        if (c.hasKey("name") && c.hasKey("globallyUniqueId")) {
          _translations[c.get("globallyUniqueId").copyString()] =
              c.get("name").copyString();
        }
      }
    } catch (std::exception const& ex) {
      LOG_TOPIC("cfaf3", ERR, Logger::REPLICATION)
          << "got error while fetching inventory: " << ex.what();
      return false;
    }
  }

  // look up cuid in translations map
  auto it = _translations.find(name.copyString());

  if (it != _translations.end()) {
    return isExcludedCollection((*it).second);
  }

  return false;
}
