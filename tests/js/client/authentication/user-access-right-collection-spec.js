/* jshint globalstrict:true, strict:true, maxlen: 5000 */
/* global describe, before, after, it */

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
// / @author Michael Hackstein
// / @author Mark Vollmary
// / @author Copyright 2017, ArangoDB GmbH, Cologne, Germany
// //////////////////////////////////////////////////////////////////////////////

'use strict';

const expect = require('chai').expect;
const users = require('@arangodb/users');
const helper = require('@arangodb/testutils/user-helper');
const errors = require('@arangodb').errors;
const namePrefix = helper.namePrefix;
const dbName = helper.dbName;
const rightLevels = helper.rightLevels;
const testColName = `${namePrefix}ColNew`;

const userSet = helper.userSet;
const systemLevel = helper.systemLevel;
const dbLevel = helper.dbLevel;
const colLevel = helper.colLevel;
const arango = require('internal').arango;
let connectionHandle = arango.getConnectionHandle();

const db = require('internal').db;
for (let l of rightLevels) {
  systemLevel[l] = new Set();
  dbLevel[l] = new Set();
  colLevel[l] = new Set();
}

helper.switchUser('root', '_system');
helper.removeAllUsers();
helper.generateAllUsers();

describe('User Rights Management', () => {
  it('should check if all users are created', () => {
    helper.switchUser('root', '_system');
    expect(userSet.size).to.be.greaterThan(0); 
    expect(userSet.size).to.equal(helper.userCount);
    for (let name of userSet) {
      expect(users.document(name), `Could not find user: ${name}`).to.not.be.undefined;
    }
  });

  it('should test rights for', () => {
    expect(userSet.size).to.be.greaterThan(0); 
    for (let name of userSet) {
      let canUse = false;
      try {
        helper.switchUser(name, dbName);
        canUse = true;
      } catch (e) {
        canUse = false;
      }

      if (canUse) {
        describe(`user ${name}`, () => {
          before(() => {
            helper.switchUser(name, dbName);
          });

          describe('administrate on db level', () => {
            const rootTestCollection = (switchBack = true) => {
              helper.switchUser('root', dbName);
              let col = db._collection(testColName);
              if (switchBack) {
                helper.switchUser(name, dbName);
              }
              return col !== null;
            };

            const rootDropCollection = () => {
              if (rootTestCollection(false)) {
                db._drop(testColName);
              }
              helper.switchUser(name, dbName);
            };

            const rootCreateCollection = () => {
              if (!rootTestCollection(false)) {
                db._create(testColName);
                if (colLevel['none'].has(name)) {
                  users.grantCollection(name, dbName, testColName, 'none');
                } else if (colLevel['ro'].has(name)) {
                  users.grantCollection(name, dbName, testColName, 'ro');
                } else if (colLevel['rw'].has(name)) {
                  users.grantCollection(name, dbName, testColName, 'rw');
                }
              }
              helper.switchUser(name, dbName);
            };

            describe('create a', () => {
              before(() => {
                db._useDatabase(dbName);
                rootDropCollection();
              });

              after(() => {
                rootDropCollection();
              });

              it('collection', () => {
                expect(rootTestCollection()).to.equal(false, 'Precondition failed, the collection still exists');
                if (dbLevel['rw'].has(name)) {
                  db._create(testColName);
                  expect(rootTestCollection()).to.equal(true, 'Collection creation reported success, but collection was not found afterwards.');
                } else {
                  try {
                    db._create(testColName);
                  } catch (e) {
                    expect(e.errorNum).to.equal(errors.ERROR_FORBIDDEN.code);
                  }
                  expect(rootTestCollection()).to.equal(false, `${name} was able to create a collection with insufficent rights`);
                }
              });
            });

            describe('drop a', () => {
              before(() => {
                db._useDatabase(dbName);
                rootCreateCollection();
              });

              after(() => {
                rootDropCollection();
              });

              it('collection', () => {
                expect(rootTestCollection()).to.equal(true, 'Precondition failed, the collection does not exist');
                if (dbLevel['rw'].has(name) && colLevel['rw'].has(name)) {
                  db._drop(testColName);
                  expect(rootTestCollection()).to.equal(false, 'Collection drop reported success, but collection was still found afterwards.');
                } else {
                  try {
                    db._drop(testColName);
                  } catch (e) {
                    expect(e.errorNum).to.equal(errors.ERROR_FORBIDDEN.code);
                  }
                  expect(rootTestCollection()).to.equal(true, `${name} was able to drop a collection with insufficent rights`);
                }
              });
            });
          });
        });
      }
    }
  });
});
after(() => {
  arango.connectHandle(connectionHandle);
  db._drop(testColName);
  db._useDatabase('_system');
  db._dropDatabase(dbName);
});
