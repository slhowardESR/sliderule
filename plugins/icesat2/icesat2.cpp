/*
 * Licensed to the University of Washington under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The University of Washington
 * licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/******************************************************************************
 *INCLUDES
 ******************************************************************************/

#include "core.h"
#include "icesat2.h"

/******************************************************************************
 * DEFINES
 ******************************************************************************/

#define LUA_ICESAT2_LIBNAME    "icesat2"

/******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * icesat2_open
 *----------------------------------------------------------------------------*/
int icesat2_open (lua_State *L)
{
    static const struct luaL_Reg icesat2_functions[] = {
        {"h5file",      Hdf5File::luaCreate},
        {"h5dataset",   Hdf5DatasetHandle::luaCreate},
        {"h5atl03",     Hdf5Atl03Handle::luaCreate},
        {"atl06",       Atl06Dispatch::luaCreate},
        {NULL,          NULL}
    };

    /* Set Globals */
    LuaEngine::setAttrInt(L, "CNF_POSSIBLE_TEP",    Hdf5Atl03Handle::CNF_POSSIBLE_TEP);
    LuaEngine::setAttrInt(L, "CNF_NOT_CONSIDERED",  Hdf5Atl03Handle::CNF_NOT_CONSIDERED);
    LuaEngine::setAttrInt(L, "CNF_BACKGROUND",      Hdf5Atl03Handle::CNF_BACKGROUND);
    LuaEngine::setAttrInt(L, "CNF_WITHIN_10M",      Hdf5Atl03Handle::CNF_WITHIN_10M);
    LuaEngine::setAttrInt(L, "CNF_SURFACE_LOW",     Hdf5Atl03Handle::CNF_SURFACE_LOW);
    LuaEngine::setAttrInt(L, "CNF_SURFACE_MEDIUM",  Hdf5Atl03Handle::CNF_SURFACE_MEDIUM);
    LuaEngine::setAttrInt(L, "CNF_SURFACE_HIGH",    Hdf5Atl03Handle::CNF_SURFACE_HIGH);
    LuaEngine::setAttrInt(L, "SRT_LAND",            Hdf5Atl03Handle::SRT_LAND);
    LuaEngine::setAttrInt(L, "SRT_OCEAN",           Hdf5Atl03Handle::SRT_OCEAN);
    LuaEngine::setAttrInt(L, "SRT_SEA_ICE",         Hdf5Atl03Handle::SRT_SEA_ICE);
    LuaEngine::setAttrInt(L, "SRT_LAND_ICE",        Hdf5Atl03Handle::SRT_LAND_ICE);
    LuaEngine::setAttrInt(L, "SRT_INLAND_WATER",    Hdf5Atl03Handle::SRT_INLAND_WATER);

    /* Set Library */
    luaL_newlib(L, icesat2_functions);

    return 1;
}

/******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************/

extern "C" {
void initicesat2 (void)
{
    LuaEngine::extend(LUA_ICESAT2_LIBNAME, icesat2_open);

    /* Indicate Presence of Package */
    LuaEngine::indicate(LUA_ICESAT2_LIBNAME, BINID);

    /* Display Status */
    printf("%s plugin initialized (%s)\n", LUA_ICESAT2_LIBNAME, BINID);
}
}
