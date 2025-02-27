/* jshint globalstrict:false, strict:false, maxlen: 200 */
/* global assertTrue, assertEqual */

// //////////////////////////////////////////////////////////////////////////////
// / DISCLAIMER
// /
// / Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
// / Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
// /
// / Licensed under the Business Source License 1.1 (the "License");
// / you may not use this file except in compliance with the License.
// / You may obtain a copy of the License at
// /
// /     https://github.com/arangodb/arangodb/blob/devel/LICENSE
// /
// / Unless required by applicable law or agreed to in writing, software
// / distributed under the License is distributed on an "AS IS" BASIS,
// / WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// / See the License for the specific language governing permissions and
// / limitations under the License.
// /
// / Copyright holder is ArangoDB GmbH, Cologne, Germany
// /
// / @author Jan Steemann
// //////////////////////////////////////////////////////////////////////////////

let jsunity = require('jsunity');
let internal = require('internal');
let fs = require('fs');
let pu = require('@arangodb/testutils/process-utils');
const tmpDirMngr = require('@arangodb/testutils/tmpDirManager').tmpDirManager;
const {sanHandler} = require('@arangodb/testutils/san-file-handler');

function arangoSecureInstallationSuite () {
  'use strict';

  let oldPasswd;

  const arangoSecureInstallation = pu.ARANGO_SECURE_INSTALLATION_BIN;

  assertTrue(fs.isFile(arangoSecureInstallation), "arango-secure-installation not found!");

  return {
    setUpAll: function () {
      oldPasswd = internal.env['ARANGODB_DEFAULT_ROOT_PASSWORD'];
    },
    
    tearDownAll: function () {
      internal.env['ARANGODB_DEFAULT_ROOT_PASSWORD'] = oldPasswd;
    },

    setUp: function () {
      delete internal.env['ARANGODB_DEFAULT_ROOT_PASSWORD'];
    },
    
    testInvokeArangoSecureInstallationWithoutPassword: function () {
      let path = fs.getTempFile();
      // database directory
      fs.makeDirectory(path);

      // set no password for the database
      try {
        let args = [path];
        // invoke arango-secure-installation without password. this will fail
        let sh = new sanHandler(pu.ARANGOD_BIN, global.instanceManager.options);
        let tmpMgr = new tmpDirMngr(fs.join('shell-arango-secure-installation-noncluster-1'), global.instanceManager.options);
        sh.detectLogfiles(tmpMgr.tempDir, tmpMgr.tempDir);
        let actualRc = internal.executeExternalAndWait(arangoSecureInstallation, args, false, 0, sh.getSanOptions());
        sh.fetchSanFileAfterExit(actualRc.pid);
        assertTrue(actualRc.hasOwnProperty("exit"), actualRc);
        assertEqual(1, actualRc.exit, actualRc);
      } finally {
        try {
          fs.removeDirectory(path);
        } catch (err) {}
      }
    },

    testInvokeArangoSecureInstallationWithPassword: function () {
      let path = fs.getTempFile();
      // database directory
      fs.makeDirectory(path);

      // set an initial password for the database
      internal.env['ARANGODB_DEFAULT_ROOT_PASSWORD'] = 'haxxmann';
      try {
        let args = [path];
        // invoke arango-secure-installation with password. this must succeed
        let sh = new sanHandler(pu.ARANGOD_BIN, global.instanceManager.options);
        let tmpMgr = new tmpDirMngr(fs.join('shell-arango-secure-installation-noncluster-2'), global.instanceManager.options);
        sh.detectLogfiles(tmpMgr.tempDir, tmpMgr.tempDir);
        let actualRc = internal.executeExternalAndWait(arangoSecureInstallation, args, false, 0, sh.getSanOptions());
        sh.fetchSanFileAfterExit(actualRc.pid);
        assertTrue(actualRc.hasOwnProperty("exit"), actualRc);
        assertEqual(0, actualRc.exit, actualRc);
      } finally {
        try {
          fs.removeDirectory(path);
        } catch (err) {}
      }
    },
    
  };
}

jsunity.run(arangoSecureInstallationSuite);

return jsunity.done();
