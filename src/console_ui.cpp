#include "console_ui.h"
// console_ui: ncurses 控制台实现，显示状态与简要信息
#include <ncurses.h>
#include <string>
#include <thread>
#include <atomic>

static std::atomic<bool> g_running{false};
static std::string g_status;
static std::string g_conn_status;
static std::string g_subs;
static std::string g_orders;

ConsoleUI::ConsoleUI() {}
ConsoleUI::~ConsoleUI() { stop(); }

void ConsoleUI::start() {
    if(g_running.exchange(true)) return;
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    std::thread([]{
        while(g_running) {
            clear();
            mvprintw(0,0, "Crypto Trading Dashboard");
            mvprintw(2,0, "Status: %s", g_status.c_str());
            mvprintw(3,0, "Conn: %s", g_conn_status.c_str());
            mvprintw(5,0, "Subs: %s", g_subs.c_str());
            mvprintw(7,0, "Orders: %s", g_orders.c_str());
            mvprintw(4,0, "Press q to quit");
            refresh();
            int ch = getch();
            if(ch == 'q') g_running = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        endwin();
    }).detach();
}

void ConsoleUI::stop() { g_running = false; }

void ConsoleUI::set_status(const std::string &s) { g_status = s; }

void ConsoleUI::set_connection_status(const std::string &s) { g_conn_status = s; }
void ConsoleUI::set_subscriptions(const std::string &s) { g_subs = s; }
void ConsoleUI::set_orders(const std::string &s) { g_orders = s; }
