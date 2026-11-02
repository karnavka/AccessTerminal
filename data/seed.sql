-- Klumba: початкові тестові дані (виконується один раз, коли таблиця users порожня)
-- Транзакцію (begin/commit) відкриває DataBaseInitializer, тут її немає.
--
-- УВАГА: pin_hash нижче це SHA-256 (hex) від PIN без солі, лише для демо.
-- Якщо HashService використовує інший алгоритм або сіль, замініть значення
-- на результат HashService::hash(pin), інакше verify() не пройде.
--
-- Тестові облікові дані:
--   ADM-001   PIN 4321   Admin (вхід через адмін-консоль, час не обмежено)
--   CARD-001  PIN 1234   Іван Петренко, пн-пт 08:00-20:00
--   CARD-002  PIN 2468   Олена Коваль,  пн-ср 09:00-18:00

INSERT INTO users (id, name, role) VALUES
    (1, 'Admin',          'Admin'),
    (2, 'Іван Петренко',  'User'),
    (3, 'Олена Коваль',   'User');

INSERT INTO cards (card_id, user_id, pin_hash, status) VALUES
    ('ADM-001',  1, 'fe2592b42a727e977f055947385b709cc82b16b9a87f88c6abf3900d65d0cdc3', 'Active'),
    ('CARD-001', 2, '03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4', 'Active'),
    ('CARD-002', 3, 'a1fb4e703a9ef1fa4936801721ff285a97ac85330856674412e054892afe6972', 'Active');

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