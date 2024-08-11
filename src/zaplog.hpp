#pragma once

#ifndef ZAPLOG_BUILDING_SLIB
#if defined _WIN32 || defined __CYGWIN__
#ifdef ZAPLOG_BUILDING
#define ZAPLOG_API __declspec(dllexport)
#else
#define ZAPLOG_API __declspec(dllimport)
#endif
#else
#ifdef ZAPLOG_BUILDING
#define ZAPLOG_API __attribute__((visibility("default")))
#else
#define ZAPLOG_API
#endif
#endif
#else
#define ZAPLOG_API
#endif

namespace zaplog {

class ZAPLOG_API Zaplog
{
  public:
    Zaplog();
    int get_number() const;

  private:
    int number;
};

} // namespace zaplog
