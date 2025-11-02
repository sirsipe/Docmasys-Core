#pragma once

namespace DB_Schema
{
  static const char *DB_SCHEMA = R"SQL(

    CREATE TABLE IF NOT EXISTS blobs (
      id        INTEGER PRIMARY KEY,
      hash      BLOB NOT NULL CHECK (length(hash) = 32),
      status    INT NOT NULL CHECK (status IN (0,1)), -- 0 not ready, 1 ready
      UNIQUE(hash)
    );

    CREATE INDEX IF NOT EXISTS idx_blobs ON blobs(hash);

    CREATE TABLE IF NOT EXISTS folders (
      id        INTEGER PRIMARY KEY,
      parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE,
      name      TEXT NOT NULL COLLATE NOCASE,
      UNIQUE(parent_id, name)
    );

    CREATE INDEX IF NOT EXISTS idx_folders_parent ON folders(parent_id);

    CREATE TABLE IF NOT EXISTS files (
      id        INTEGER PRIMARY KEY,
      parent_id INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE,
      name      TEXT NOT NULL COLLATE NOCASE,
      blob_id   INTEGER NOT NULL REFERENCES blobs(id) ON DELETE RESTRICT,
      UNIQUE(parent_id, name)
    );

    CREATE INDEX IF NOT EXISTS idx_files_parent ON files(parent_id);
    CREATE INDEX IF NOT EXISTS idx_files_blob ON files(blob_id);

    -- After DELETE on files: remove orphaned blob
    CREATE TRIGGER IF NOT EXISTS trg_files_ad_del_orphan_blob
    AFTER DELETE ON files
    BEGIN
      DELETE FROM blobs
      WHERE id = OLD.blob_id
      AND NOT EXISTS (SELECT 1 FROM files WHERE blob_id = OLD.blob_id);
    END;

    -- After UPDATE of blob_id: remove old blob if now orphaned
    CREATE TRIGGER IF NOT EXISTS trg_files_au_blob_orphan
    AFTER UPDATE OF blob_id ON files
    BEGIN
      DELETE FROM blobs
      WHERE id = OLD.blob_id
      AND NOT EXISTS (SELECT 1 FROM files WHERE blob_id = OLD.blob_id);
    END;
  
  )SQL";
}