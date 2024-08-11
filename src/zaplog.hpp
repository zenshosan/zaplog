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

#include <string>
namespace zaplog {

class ZAPLOG_API Zaplog
{
  public:
    Zaplog();
    int get_number() const;
    std::string get_version() const;
    std::string get_git_hash() const;
    std::string get_git_date() const;

  private:
    int number;
};

} // namespace zaplog
