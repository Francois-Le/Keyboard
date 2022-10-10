#define DEBUG_LOG 1
#define I2C_RESET_LOG 1
#define PERF_LOG 0

#if DEBUG_LOG
#define debugPrint(x) Serial.print(x)
#define debugPrintln(x) Serial.println(x)
#define ON_DEBUG(x) x
#else
#define debugPrint(x)
#define debugPrintln(x)
#define ON_DEBUG(x)
#endif
