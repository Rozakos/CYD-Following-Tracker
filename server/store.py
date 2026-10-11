"""SQLite follower history. Neither YouTube nor Twitch exposes past follower
counts, so history is whatever this server has recorded since it started
tracking an account (the poller samples every tracked account periodically).
"""
from __future__ import annotations

import sqlite3
import threading
import time


class Store:
    def __init__(self, path: str):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.lock = threading.Lock()
        with self.lock, self.db:
            self.db.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS samples (
                    platform TEXT NOT NULL, handle TEXT NOT NULL,
                    ts INTEGER NOT NULL, followers INTEGER NOT NULL,
                    PRIMARY KEY (platform, handle, ts));
                CREATE TABLE IF NOT EXISTS accounts (
                    platform TEXT NOT NULL, handle TEXT NOT NULL,
                    display_name TEXT, last_requested INTEGER NOT NULL,
                    PRIMARY KEY (platform, handle));
            """)

    def add_sample(self, platform, handle, followers, ts=None, min_gap=0):
        """Record a count; skipped when the previous sample is newer than min_gap seconds."""
        ts = int(ts if ts is not None else time.time())
        with self.lock, self.db:
            if min_gap:
                last = self.db.execute("SELECT MAX(ts) FROM samples WHERE platform=? AND handle=?",
                                       (platform, handle)).fetchone()[0]
                if last is not None and ts - last < min_gap:
                    return False
            self.db.execute("INSERT OR REPLACE INTO samples VALUES (?,?,?,?)", (platform, handle, ts, int(followers)))
        return True

    def add_samples(self, platform, handle, rows):
        with self.lock, self.db:
            self.db.executemany("INSERT OR IGNORE INTO samples VALUES (?,?,?,?)",
                                [(platform, handle, int(ts), int(f)) for ts, f in rows])

    def samples(self, platform, handle, since=0):
        with self.lock:
            return self.db.execute(
                "SELECT ts, followers FROM samples WHERE platform=? AND handle=? AND ts>=? ORDER BY ts",
                (platform, handle, int(since))).fetchall()

    def first_ts(self, platform, handle):
        with self.lock:
            return self.db.execute("SELECT MIN(ts) FROM samples WHERE platform=? AND handle=?",
                                   (platform, handle)).fetchone()[0]

    def value_at(self, platform, handle, ts):
        """Latest recorded count at or before ts, or None."""
        with self.lock:
            row = self.db.execute(
                "SELECT followers FROM samples WHERE platform=? AND handle=? AND ts<=? ORDER BY ts DESC LIMIT 1",
                (platform, handle, int(ts))).fetchone()
        return row[0] if row else None

    def touch(self, platform, handle, display_name=None):
        with self.lock, self.db:
            self.db.execute(
                "INSERT INTO accounts VALUES (?,?,?,?) ON CONFLICT(platform, handle) DO UPDATE SET "
                "last_requested=excluded.last_requested, display_name=COALESCE(excluded.display_name, display_name)",
                (platform, handle, display_name, int(time.time())))

    def active_accounts(self, within_seconds):
        with self.lock:
            return self.db.execute("SELECT platform, handle FROM accounts WHERE last_requested>=?",
                                   (int(time.time() - within_seconds),)).fetchall()

    def prune(self, keep_days):
        with self.lock, self.db:
            self.db.execute("DELETE FROM samples WHERE ts < ?", (int(time.time() - keep_days * 86400),))
