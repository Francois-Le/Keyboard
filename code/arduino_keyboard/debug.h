#pragma once

#define DEBUG_LOG 0
#define I2C_RESET_LOG 0
#define PERF_LOG_CORE_0 1
#define PERF_LOG_CORE_1 0

#if PERF_LOG_CORE_0 && PERF_LOG_CORE_1
#error cant perf both cores at the same time
#endif

#if DEBUG_LOG
#define debugPrint(x) Serial.print(x)
#define debugPrintln(x) Serial.println(x)
#define ON_DEBUG(x) x
#else
#define debugPrint(x)
#define debugPrintln(x)
#define ON_DEBUG(x)
#endif

class PerformanceMonitor
{
  public:
    PerformanceMonitor()
    {
      reset();
    }

    inline void start()
    {
      m_lastStart = micros();
    }

    inline void end(const char* name)
    {
      unsigned long total = micros() - m_lastStart;
      
      m_min = min(m_min, total);
      m_max = max(m_max, total);
      m_total += total;
      m_count++;
      if (m_count == s_countTotal)
      {
        unsigned long avg = m_total / s_countTotal;
        Serial.print(name);
        Serial.print(": perf min=");
        Serial.print(m_min);
        Serial.print("us max=");
        Serial.print(m_max);
        Serial.print("us avg=");
        Serial.print(avg);
        Serial.println("us");

        reset();
      }
    }

  private:
    inline void reset()
    {
      m_min = -1;
      m_max = 0;
      m_total = 0;
      m_count = 0;
    }

    unsigned long m_min;
    unsigned long m_max;
    unsigned long m_total;
    int m_count;
    unsigned long m_lastStart;

    static constexpr int s_countTotal = 1000;
};
