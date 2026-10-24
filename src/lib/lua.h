#ifndef LIB_LUA_H_
#define LIB_LUA_H_

#include <memory>
#include <functional>
#include <string>
#include <atomic>
#include "result.h"

struct lua_State;

// Teardown signal, set by the *rime integration layer* (modules.cc) once the
// rime service is being finalized.  The lib/ layer only *observes* this flag
// (skips lua_close / GC / registry unref); it never decides on its own, so
// src/lib stays free of any rime dependency (upstream keeps this directory
// rime-free, see hchunhui/librime-lua).
#if defined(__GNUC__)
#define LUA_TEARDOWN_LOCAL __attribute__((visibility("hidden")))
#else
#define LUA_TEARDOWN_LOCAL
#endif
extern LUA_TEARDOWN_LOCAL std::atomic<bool> lua_shutting_down;

// Latches lua_shutting_down and returns true when the rime service has been
// stopped for good (!started_ && not in background maintenance), i.e. during
// RimeFinalize().  Defined in the integration layer (modules.cc).
LUA_TEARDOWN_LOCAL bool check_lua_shutting_down();

class LuaObj {
public:
  ~LuaObj();

  static void pushdata(lua_State *L, std::shared_ptr<LuaObj> &o);
  static std::shared_ptr<LuaObj> todata(lua_State *L, int i);

private:
  LuaObj(lua_State *L, int i);
  lua_State *L_;
  int id_;
};

struct LuaErr { int status; std::string e; };
template <typename T>
using LuaResult = Result<T, LuaErr>;

class Lua {
public:
  Lua();
  ~Lua();

  std::shared_ptr<LuaObj> getglobal(const std::string &f);

  std::shared_ptr<LuaObj> newthreadx(lua_State *L, int nargs);

  void gc();
  void gc_step(int kb);

  template <typename ... I>
  std::shared_ptr<LuaObj> newthread(I ... input);

  template <typename O>
  LuaResult<O> resume(std::shared_ptr<LuaObj> f);

  template <typename O, typename ... I>
  LuaResult<O> call(I ... input);

  template <typename ... I>
  LuaResult<void> void_call(I ... input);

  void to_state(std::function<void (lua_State *)> f);

  static Lua *from_state(lua_State *L);
private:
  lua_State *L_;
  // 析构期间为真（此时 lua_close 内部的 GC 正在跑，见 lua.cc 的两个守卫）
  bool closing_ = false;
};

namespace LuaImpl {
  int wrap_common(lua_State *L, int (*cfunc)(lua_State *));
}

#endif  // LIB_LUA_H_
