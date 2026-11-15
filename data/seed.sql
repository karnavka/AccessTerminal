-- Klumba: початкові тестові дані (виконується один раз, коли таблиця users порожня)
-- Транзакцію (begin/commit) відкриває DataBaseInitializer, тут її немає.
--
-- УВАГА: pin_hash нижче це SHA-256 (hex) від PIN без солі, лише для демо.
-- Якщо HashService використовує інший алгоритм або сіль, замініть значення
-- на результат HashService::hash(pin), інакше verify() не пройде.
--
-- Тестові облікові дані:
--   ADM-001   PIN 0000   Admin (вхід через адмін-консоль, час не обмежено)
--   CARD-001  PIN 0000   Іван Петренко, пн-пт 08:00-20:00
--   CARD-002  PIN 0000   Олена Коваль,  пн-ср 09:00-18:00

INSERT INTO users (id, name, role) VALUES
    (1, 'Admin',          'Admin'),
    (2, 'Іван Петренко',  'User'),
    (3, 'Олена Коваль',   'User');

INSERT INTO cards (card_id, user_id, pin_hash, status) VALUES
    ('ADM-001',  1, '9af15b336e6a9619928537df30b2e6a2376569fcf9d7e773eccede65606529a0', 'Active'),
    ('CARD-001', 2, '9af15b336e6a9619928537df30b2e6a2376569fcf9d7e773eccede65606529a0', 'Active'),
    ('CARD-002', 3, '9af15b336e6a9619928537df30b2e6a2376569fcf9d7e773eccede65606529a0', 'Active');

-- day_of_week: 0 = понеділок ... 6 = неділя; хвилини від початку доби
-- ADM-001: щодня, цілодобово
INSERT INTO card_time_windows (card_id, day_of_week, start_minute, end_minute) VALUES
    ('ADM-001', 0, 0, 1440), ('ADM-001', 1, 0, 1440), ('ADM-001', 2, 0, 1440),
    ('ADM-001', 3, 0, 1440), ('ADM-001', 4, 0, 1440), ('ADM-001', 5, 0, 1440),
    ('ADM-001', 6, 0, 1440);

-- CARD-001: пн-пт 08:00-20:00
INSERT INTO card_time_windows (card_id, day_of_week, start_minute, end_minute) VALUES
    ('CARD-001', 0, 480, 1200), ('CARD-001', 1, 480, 1200), ('CARD-001', 2, 480, 1200),
    ('CARD-001', 3, 480, 1200), ('CARD-001', 4, 480, 1200);

-- CARD-002: пн-ср 09:00-18:00
INSERT INTO card_time_windows (card_id, day_of_week, start_minute, end_minute) VALUES
    ('CARD-002', 0, 540, 1080), ('CARD-002', 1, 540, 1080), ('CARD-002', 2, 540, 1080);