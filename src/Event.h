#pragma once
#include <string>
#include <tchar.h>

enum class LoggingRunningState { NotRunning, Running };

class Event {
private:
  std::string description;
  std::string start_date;
  std::string end_date;
  int id;

public:
  Event(const char *description, const char *start_date, const char *end_date,
        int id)
      : description(description), start_date(start_date), end_date(end_date),
        id(id) {}
  Event(const char *description, const char *start_date)
      : description(description), start_date(start_date) {}
  Event(const char *start_date) : start_date(start_date) {}
  std::string get_description() { return description; }
  std::string get_start_date() { return start_date; }
  std::string get_end_date() { return end_date; }
  int get_id() { return id; }

  void set_description(const TCHAR *description) {
    this->description = reinterpret_cast<const char *>(description);
  }
  void set_end_date(const char *end_date) { this->end_date = end_date; }
  void set_id(int id) { this->id = id; }
};