-- Klumba: схема БД терміналу контролю доступу (SQLite)
-- Сутності: UserEntity, CardEntity (+ TimeWindow), LogEntity

-- ---------------------------------------------------------------
-- UserEntity
-- ---------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    name  TEXT NOT NULL CHECK (length(trim(name)) > 0),
    role  TEXT NOT NULL DEFAULT 'User' CHECK (role IN ('User', 'Admin'))
);

-- ---------------------------------------------------------------
-- CardEntity
-- ---------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cards (
    card_id         TEXT PRIMARY KEY CHECK (length(trim(card_id)) > 0),
    user_id         INTEGER NOT NULL REFERENCES users(id),
    pin_hash        TEXT NOT NULL,
    status          TEXT NOT NULL DEFAULT 'Active'
                    CHECK (status IN ('Active', 'Blocked', 'Revoked')),
    failed_attempts INTEGER NOT NULL DEFAULT 0 CHECK (failed_attempts >= 0),
    blocked_until   TEXT NULL,
    CHECK (blocked_until IS NULL OR status = 'Blocked')
);

-- нам треба тільки 1 картка активна на корстувача??
-- CREATE UNIQUE INDEX IF NOT EXISTS ux_cards_one_live_per_user
--     ON cards(user_id) WHERE status <> 'Revoked';

-- ---------------------------------------------------------------
-- TimeWindow
-- day_of_week: 0 = понеділок ... 6 = неділя
-- start_minute / end_minute: хвилини від початку доби, [start, end)
-- ---------------------------------------------------------------
CREATE TABLE IF NOT EXISTS card_time_windows (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    card_id      TEXT NOT NULL REFERENCES cards(card_id) ON DELETE CASCADE,
    day_of_week  INTEGER NOT NULL CHECK (day_of_week BETWEEN 0 AND 6),
    start_minute INTEGER NOT NULL CHECK (start_minute BETWEEN 0 AND 1439),
    end_minute   INTEGER NOT NULL CHECK (end_minute BETWEEN 1 AND 1440),
    CHECK (start_minute < end_minute),
    UNIQUE (card_id, day_of_week, start_minute, end_minute)
);

CREATE INDEX IF NOT EXISTS ix_time_windows_card ON card_time_windows(card_id);

-- ---------------------------------------------------------------
-- LogEntity
-- Час зберігається як TEXT у форматі ISO 8601 UTC ('YYYY-MM-DDTHH:MM:SSZ').
-- ---------------------------------------------------------------
CREATE TABLE IF NOT EXISTS access_log (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    card_id   TEXT NULL,
    result    TEXT NOT NULL CHECK (result IN ('Success', 'Denied', 'Alarm')),
    reason    TEXT NULL CHECK (reason IN (
                  'WrongPin', 'UnknownCard', 'Revoked', 'Blocked',
                  'OutsideTimeWindow', 'DbUnavailable',
                  'TooManyAttempts', 'ForcedOpen')),
    CHECK (
        (result = 'Success' AND reason IS NULL AND card_id IS NOT NULL)
        OR (result IN ('Denied', 'Alarm') AND reason IS NOT NULL)
    )
);

CREATE INDEX IF NOT EXISTS ix_log_timestamp ON access_log(timestamp);
CREATE INDEX IF NOT EXISTS ix_log_card      ON access_log(card_id);
CREATE INDEX IF NOT EXISTS ix_log_result    ON access_log(result);