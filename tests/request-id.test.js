import {test} from 'node:test';
import assert from 'node:assert/strict';
import {requestId} from '../src/request-id.js';
test('fallback creates RFC 4122 v4 ids from secure randomness',()=>{
  const id=requestId({getRandomValues: array=>array.fill(0)});
  assert.equal(id,'00000000-0000-4000-8000-000000000000');
});
test('refuses an insecure random source',()=>assert.throws(()=>requestId({}),/HTTPS/));
