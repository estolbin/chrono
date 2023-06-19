#include "sqlite3.h"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>


class SQLiteConnectionPool {
public:
  SQLiteConnectionPool(const std::string &filename, int pool_size = 5)
      : filename_(filename) {
    for (int i = 0; i < pool_size; i++) {
      sqlite3 *db;
      sqlite3_open(filename.c_str(), &db);
      pool_.push(db);
    }
  }

  sqlite3 *acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (pool_.empty()) {
      cv_.wait(lock);
    }

    sqlite3 *db = pool_.front();
    pool_.pop();
    return db;
  }

  void release(sqlite3 *db) {
    std::unique_lock<std::mutex> lock(mutex_);
    pool_.push(db);
    cv_.notify_one();
  }

private:
  std::string filename_;
  std::queue<sqlite3 *> pool_;
  std::mutex mutex_;
  std::condition_variable cv_;
};