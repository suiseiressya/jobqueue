#include<bits/stdc++.h>

using namespace std;

struct KVStore {
private:
    unordered_map<string, string> _store;
    std::mutex mt;
    std::mutex mt_store;
    std::mutex mt_cout;
public:
    // lock_guard: most basic RAII lock control
    void set(string key, string value) {
        std::lock_guard<std::mutex> guard(mt);
        _store[key] = value;
    }

    void get(string key) {
        std::lock_guard<std::mutex> guard(mt);
        if (_store.find(key) == _store.end()) {
            cout << "string not found";
            return;
        }
        cout << _store[key];
        cout << endl;
    }

    // unique_lock: more fine-grained but more manual lock control 
    // have to manually lock/unlock
    void set_unique(string key, string value) {
        std::unique_lock<std::mutex> lock(mt);
        _store[key] = value;
        lock.unlock();
    }

    void get_unique(string key) {
        std::unique_lock<std::mutex> lock(mt);
        if (_store.find(key) == _store.end()) {
            lock.unlock();
            cout << "string not found" << endl;
            return;
        }
        string val = _store[key];
        lock.unlock();
        cout << val << endl;
    }

    // scoped_lock: lock multiple keys 
    // effectively replaced std::lock when locking multiple mutexes
    void set_scoped(string key, string value) {
        std::scoped_lock lock(mt_store);
        _store[key] = value;
    }

    void get_scoped(string key) {
        std::scoped_lock lock(mt_store, mt_cout);
        if (_store.find(key) == _store.end()) {
            cout << "string not found" << endl;
            return;
        }
        cout << _store[key] << endl;
    }

    int size() { return _store.size(); }
};

void basic_lock_guard() {
    KVStore store;

    std::thread thread1([&]() {
        store.set("1", "2");
        store.get("1");
    });
    std::thread thread2([&]() {
        store.set("3", "4");
        store.get("3");
    });
    std::thread thread3([&]() {
        store.set("5", "6");
        store.get("5");
    });

    thread1.join();
    thread2.join();
    thread3.join();
}

void unique_lock_demo() {
    KVStore store;

    std::thread thread1([&]() {
        store.set_unique("1", "2");
        store.get_unique("1");
    });
    std::thread thread2([&]() {
        store.set_unique("3", "4");
        store.get_unique("3");
    });
    std::thread thread3([&]() {
        store.set_unique("5", "6");
        store.get_unique("5");
    });

    thread1.join();
    thread2.join();
    thread3.join();
}

void scoped_lock_demo() {
    KVStore store;

    std::thread thread1([&]() {
        store.set_scoped("1", "2");
        store.get_scoped("1");
    });
    std::thread thread2([&]() {
        store.set_scoped("3", "4");
        store.get_scoped("3");
    });
    std::thread thread3([&]() {
        store.set_scoped("5", "6");
        store.get_scoped("5");
    });

    thread1.join();
    thread2.join();
    thread3.join();
}

void basic_stress_test() {
    KVStore store;
    vector<std::thread> threads;

    for (int i = 0; i < 100; i++) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 10000; j++) {
                store.set(to_string(i) + to_string(j), to_string(j) + to_string(i));
            }
        });
    }

    for (auto& t: threads) t.join();
}

/*
Testing:
g++ -fsanitize=thread -g -O1 kvstore.cpp -o kvstore
  ./kvstore

g++ -g -O0 kvstore.cpp -o kvstore -lpthread
  valgrind --leak-check=full ./kvstore

RECOMPILE AGAIN. cannot run both at the same time. 

*/

int main() {
    basic_stress_test();
}   