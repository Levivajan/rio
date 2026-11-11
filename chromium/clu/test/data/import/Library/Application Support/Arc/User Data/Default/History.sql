-- Sidebar-file History fixture: a Chromium History file of the sidebar file's default
-- profile, read by the same reader as Chrome's. {{NOW}} is replaced with the build time in Chrome microseconds.
CREATE TABLE meta (key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);
CREATE TABLE urls (id INTEGER PRIMARY KEY AUTOINCREMENT, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0 NOT NULL,
  typed_count INTEGER DEFAULT 0 NOT NULL, last_visit_time INTEGER NOT NULL, hidden INTEGER DEFAULT 0 NOT NULL);
CREATE TABLE visits (id INTEGER PRIMARY KEY, url INTEGER NOT NULL, visit_time INTEGER NOT NULL, from_visit INTEGER, transition INTEGER DEFAULT 0 NOT NULL,
  segment_id INTEGER, visit_duration INTEGER DEFAULT 0 NOT NULL);
INSERT INTO meta VALUES ('version', '70');
INSERT INTO urls (url, title, visit_count, typed_count, last_visit_time, hidden) VALUES
  ('https://lantern.example/', 'Lantern', 4, 1, {{NOW}} - 3600000000, 0),
  ('https://orchard.example/plans', 'Orchard plans', 1, 0, {{NOW}} - 2 * 86400000000, 0),
  ('https://old.example/', 'Too old', 3, 0, {{NOW}} - 120 * 86400000000, 0),
  ('chrome://settings/', 'Settings', 1, 0, {{NOW}} - 86400000000, 0);
INSERT INTO visits (url, visit_time, transition) SELECT id, last_visit_time, 1 FROM urls;
