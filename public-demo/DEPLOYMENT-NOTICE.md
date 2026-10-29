# Isolated demo deployment status

The checked-in HTML is a candidate artifact, not an available public website.
The embedded `demo1` URL was a trial placeholder. Supabase Edge Functions force
HTML responses to `text/plain` and `Content-Security-Policy: default-src 'none'; sandbox`,
so the two trial functions were removed. The placeholder must be changed to
an approved static hosting URL, and both artifacts rebuilt before publishing.
The isolated-site-probe workflow is manual until a real hosting target exists.
No production Pages or production database was changed by this experiment.
