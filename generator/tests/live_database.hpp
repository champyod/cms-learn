#pragma once

// The rule for finding a live database lives in core, because the generator and the api both need it and the
// api must stay optional for this build. The names are aliased into this tree's namespace so its cases keep
// calling them where they always have.

#include <cms/testing/live_database.hpp>

namespace cms::generate::test {

using cms::test::has_live_database;
using cms::test::live_client;
using cms::test::live_database;

}  // namespace cms::generate::test
