#pragma once

// The rule for finding a live database lives in core, because the api and the generator both need it and the
// api must stay optional for the generator's build. This header is the include the api's cases already name.

#include <cms/testing/live_database.hpp>
