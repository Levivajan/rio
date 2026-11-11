-- Chrome History fixture (07-data R8): the tables the importer reads, with Chrome's column names.
-- {{NOW}} is replaced with the build time in Chrome microseconds since 1601-01-01; one day is 86400000000.
CREATE TABLE meta (key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);
CREATE TABLE urls (id INTEGER PRIMARY KEY AUTOINCREMENT, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0 NOT NULL,
  typed_count INTEGER DEFAULT 0 NOT NULL, last_visit_time INTEGER NOT NULL, hidden INTEGER DEFAULT 0 NOT NULL);
CREATE TABLE visits (id INTEGER PRIMARY KEY, url INTEGER NOT NULL, visit_time INTEGER NOT NULL, from_visit INTEGER, transition INTEGER DEFAULT 0 NOT NULL,
  segment_id INTEGER, visit_duration INTEGER DEFAULT 0 NOT NULL);
INSERT INTO meta VALUES ('version', '70');
INSERT INTO urls (url, title, visit_count, typed_count, last_visit_time, hidden) VALUES
  ('https://harbor.example/', 'Harbor', 5, 2, {{NOW}} - 3600000000, 0),
  ('https://kettle.example/notes', 'Kettle notes', 1, 0, {{NOW}} - 3 * 86400000000, 0),
  ('http://meadow.example/', 'Meadow', 2, 0, {{NOW}} - 30 * 86400000000, 0),
  ('https://docs.example/guide', '', 1, 0, {{NOW}} - 89 * 86400000000, 0),
  ('https://old.example/', 'Too old', 3, 0, {{NOW}} - 120 * 86400000000, 0),
  ('https://hidden.example/', 'Hidden', 1, 0, {{NOW}} - 86400000000, 1),
  ('chrome://settings/', 'Settings', 4, 0, {{NOW}} - 86400000000, 0),
  ('file:///Users/someone/notes.html', 'Local file', 1, 0, {{NOW}} - 86400000000, 0),
  ('javascript:void(0)', 'Script', 1, 0, {{NOW}} - 86400000000, 0),
  ('https://future.example/', 'Future', 1, 0, {{NOW}} + 86400000000, 0);
INSERT INTO visits (url, visit_time, transition) SELECT id, last_visit_time, 1 FROM urls;
