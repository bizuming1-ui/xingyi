import { test } from 'node:test';
import assert from 'node:assert/strict';
import { publicAppUrl } from '../src/public-url.js';
const site='https://bizuming1-ui.github.io/xingyi/';
test('Arena previews encode public Pages URL instead of private preview URL',()=>{
  assert.equal(publicAppUrl(new URL('https://example.arena.site/'),'/'),site);
  assert.equal(publicAppUrl(new URL('https://5173-example.e2b.app/'),'/'),site);
});
test('production uses its own URL and override supports a custom deployment',()=>{
  assert.equal(publicAppUrl(new URL(site),'/xingyi/'),site);
  assert.equal(publicAppUrl(new URL('https://preview.arena.site/'),'/', 'https://example.com/app/'), 'https://example.com/app/');
});
