// Test runner for the librime-lua plugin, modeled on librime's
// test/rime_test_main.cc.  All data directories point to the working
// directory so the tests can deploy their own scripts under ./lua/.
#include <gtest/gtest.h>
#include <rime_api.h>
#include <rime/setup.h>

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  RIME_STRUCT(RimeTraits, traits);
  // put all files in the working directory.
  traits.shared_data_dir =
      traits.user_data_dir =
          traits.prebuilt_data_dir =
              traits.staging_dir = ".";
  rime::SetupDeployer(&traits);
  rime::SetupLogging("rime.lua.test");

  // The 'lua' module self-registers at load time (RIME_MODULE_INITIALIZER
  // in modules.cc); loading it registers the lua_* components.
  RIME_MODULE_LIST(kLuaTestModules, "lua");
  rime::LoadModules(kLuaTestModules);

  return RUN_ALL_TESTS();
}
