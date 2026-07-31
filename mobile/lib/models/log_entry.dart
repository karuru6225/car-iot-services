enum LogType { sys, rx, tx, err }

class LogEntry {
  final DateTime time;
  final String msg;
  final LogType type;
  LogEntry(this.msg, this.type) : time = DateTime.now();
}
