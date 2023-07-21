// Copyright 2020 ETH Zurich
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdint.h>
#include "gdriver.h"

int main(int argc, char**argv)
{
    const char *handlers_file = "build/copy_from_host";
    const char *hh = NULL;
    const char *ph = "copy_from_host_ph";
    const char *th = NULL;
    int num_ectx;

    gdriver_init(argc, argv, NULL, &num_ectx);
    gdriver_add_ectx(handlers_file, hh, ph, th, NULL, NULL, 0, NULL, 0);

    gdriver_run();

    gdriver_fini();

    return 0;
}
