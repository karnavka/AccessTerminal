#pragma once

/*==================
 * Імітація апаратної частини дверей (замок + датчик), що виходить за межі проєкту.
 * Потрібна, щоб TerminalController працював так само, як із справжнім обладнанням:
 * контролер лише керує замком і читає датчики,
 * "фізичний світ" імітується ззовні.
 * ==================
 * замок (locked) - виконавчий пристрій, контролює контролер
 * двері (open)   - показання датчика, змінюється фізично (людина / зламник)
 * ==================
 */

class DoorSimulator {
public:
    //програмоване відкриття / закриття
    void unlock() {
        _locked = false;
    }
    void lock() {
        _locked = true;
    }

    //датчик
    bool isLocked() const {
        return _locked;
    }
    bool isOpen() const {
        return _open;
    }

    // фізичне відкриття-закриття
    void simulateDoorOpened() {
        _open = true;
    }
    void simulateDoorClosed() {
        _open = false;
    }

private:
    bool _locked = true;
    bool _open = false;
};