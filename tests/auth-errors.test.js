import {test} from 'node:test';
import assert from 'node:assert/strict';
import {loginErrorMessage} from '../src/auth-errors.js';

test('login errors distinguish credentials, confirmation, limits and connectivity', () => {
  assert.match(loginErrorMessage({code:'invalid_credentials',status:400}), /账号尚未创建/);
  assert.match(loginErrorMessage({code:'email_not_confirmed',status:400}), /尚未确认/);
  assert.match(loginErrorMessage({status:429}), /过于频繁/);
  assert.match(loginErrorMessage({}), /网络/);
  assert.match(loginErrorMessage({status:503}), /网络/);
});
