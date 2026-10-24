// Regression tests for the engine-shutdown crash reported against
// ibus-engine-rime (SIGSEGV in LuaFilter::~LuaFilter during RimeFinalize).
//
// In real engines, component destructors run during
// Service::CleanupAllSessions() (the engine tears down its filters and
// translators), which RimeFinalize() invokes right after StopService().
// These tests drive the same destructors against the same service state.
//
// Note: the tests below run in declaration order and share the
// Service singleton; the last one tears the module down for good.
#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <rime/candidate.h>
#include <rime/deployer.h>
#include <rime/filter.h>
#include <rime/module.h>
#include <rime/registry.h>
#include <rime/service.h>
#include <rime/ticket.h>
#include <rime/translation.h>
#include <rime_api.h>

#include "lib/lua.h"

using namespace rime;

namespace {

// require()d via the "*fini_marker" prescription; leaves a trace when the
// script's fini() callback runs.
const char* kFiniMarkerModule = R"lua(
-- test-only module: leaves a trace when fini() runs
local marker = "fini.marker"
local M = {}
M.func = function(input, env)
end
M.fini = function(env)
  local f = io.open(marker, "a")
  if f then
    f:write("fini\n")
    f:close()
  end
end
return M
)lua";

string ReadMarker() {
  std::ifstream in("fini.marker");
  if (!in.is_open())
    return "";
  return string(std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
}

class LuaTeardownTest : public ::testing::Test {
 protected:
  void SetUp() override {
    remove("fini.marker");
    std::filesystem::create_directories("lua");
    std::ofstream out("lua/fini_marker.lua");
    out << kFiniMarkerModule;
  }

  // Creates a lua_filter whose fini() writes to the marker file.
  Filter* CreateFiniMarkerFilter() {
    auto* component = Filter::Require("lua_filter");
    if (!component)
      return {};
    // LuaComponent::Create() rebuilds the ticket with klass = name_space,
    // so the Lua module is selected through the name space.
    Ticket ticket(static_cast<Engine*>(nullptr), "*fini_marker");
    return component->Create(ticket);
  }
};

class SlowMaintenanceTask : public DeploymentTask {
 public:
  bool Run(Deployer* deployer) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
  }
};

// While the service is running (e.g. during a schema switch), destroying
// the component must still invoke the script's fini() callback.
TEST_F(LuaTeardownTest, FiniRunsOnNormalComponentDestruction) {
  Service::instance().StartService();
  ASSERT_FALSE(Service::instance().disabled());

  auto filter = CreateFiniMarkerFilter();
  ASSERT_TRUE(filter != nullptr);

  delete filter;
  EXPECT_EQ("fini\n", ReadMarker());
  EXPECT_FALSE(lua_shutting_down.load());
}

// During background maintenance, Service::disabled() is true because
// Deployer::IsMaintenanceMode() is true, even though the service has NOT
// been stopped. Destroying a component then must still run fini() and must
// not flip lua_shutting_down.
TEST_F(LuaTeardownTest, FiniRunsDuringMaintenanceMode) {
  Service::instance().StartService();
  auto filter = CreateFiniMarkerFilter();
  ASSERT_TRUE(filter != nullptr);

  Deployer& deployer = Service::instance().deployer();
  deployer.ScheduleTask(New<SlowMaintenanceTask>());
  ASSERT_TRUE(deployer.StartMaintenance());
  ASSERT_TRUE(deployer.IsMaintenanceMode());
  ASSERT_TRUE(Service::instance().disabled());

  delete filter;
  EXPECT_EQ("fini\n", ReadMarker());
  EXPECT_FALSE(lua_shutting_down.load());

  deployer.JoinMaintenanceThread();
}

// Even if all sessions/components were already destroyed before RimeFinalize(),
// Registry::Clear() runs before ModuleManager::UnloadModules() and must still
// detect teardown in Lua::~Lua() and skip lua_close().
TEST_F(LuaTeardownTest, TeardownWithoutActiveComponentsStillSkipsLuaClose) {
  Service::instance().StartService();
  ASSERT_FALSE(Service::instance().disabled());

  RimeJoinMaintenanceThread();
  Service::instance().StopService();
  ASSERT_TRUE(Service::instance().disabled());
  EXPECT_FALSE(lua_shutting_down.load());

  Registry::instance().Clear();
  EXPECT_TRUE(lua_shutting_down.load());
  ModuleManager::instance().UnloadModules();

  // Re-initialize the lua module for the next test (mirrors RimeInitialize
  // after RimeFinalize in the same process).
  RimeModule* lua_mod = ModuleManager::instance().Find("lua");
  ASSERT_TRUE(lua_mod != nullptr);
  ModuleManager::instance().LoadModule(lua_mod);
  EXPECT_FALSE(lua_shutting_down.load());
}

// After StopService() (i.e. during RimeFinalize), running fini(), lua_gc(),
// or lua_close() would touch rime objects already destroyed during teardown.
// All must be skipped, and the teardown sequence must not crash.
TEST_F(LuaTeardownTest, TeardownSkipsFiniAndLuaClose) {
  Service::instance().StartService();
  ASSERT_FALSE(Service::instance().disabled());

  auto filter = CreateFiniMarkerFilter();
  ASSERT_TRUE(filter != nullptr);
  CandidateList cands;
  an<Translation> translation = filter->Apply(nullptr, &cands);
  ASSERT_TRUE(translation != nullptr);

  // Mirror RimeFinalize() (rime_api.cc).
  RimeJoinMaintenanceThread();
  Service::instance().StopService();
  ASSERT_TRUE(Service::instance().disabled());

  // In the engine this happens inside CleanupAllSessions(): the engine
  // destroys its filters first, then Engine::~Engine() resets context_
  // (which destroys any active LuaTranslation in the composition menu).
  delete filter;
  EXPECT_EQ("", ReadMarker());  // fini() was skipped
  EXPECT_TRUE(lua_shutting_down.load());

  translation.reset();  // LuaTranslation::~LuaTranslation must skip lua_->gc()

  // Drops the last an<Lua> -> Lua::~Lua; must not lua_close() the state.
  Registry::instance().Clear();
  ModuleManager::instance().UnloadModules();
  EXPECT_TRUE(lua_shutting_down.load());
  // Reaching these expectations means the teardown sequence did not crash.
}

}  // namespace
