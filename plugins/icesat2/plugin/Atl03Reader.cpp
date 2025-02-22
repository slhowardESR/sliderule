/*
 * Copyright (c) 2021, University of Washington
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the University of Washington nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY OF WASHINGTON AND CONTRIBUTORS
 * “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY OF WASHINGTON OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/******************************************************************************
 * INCLUDES
 ******************************************************************************/

#include <math.h>
#include <float.h>
#include <stdarg.h>

#include "core.h"
#include "h5.h"
#include "icesat2.h"

/******************************************************************************
 * STATIC DATA
 ******************************************************************************/

const char* Atl03Reader::phRecType = "atl03rec.photons";
const RecordObject::fieldDef_t Atl03Reader::phRecDef[] = {
    {"time",            RecordObject::TIME8,    offsetof(photon_t, time_ns),        1,  NULL, NATIVE_FLAGS},
    {"latitude",        RecordObject::DOUBLE,   offsetof(photon_t, latitude),       1,  NULL, NATIVE_FLAGS},
    {"longitude",       RecordObject::DOUBLE,   offsetof(photon_t, longitude),      1,  NULL, NATIVE_FLAGS},
    {"x_atc",           RecordObject::FLOAT,    offsetof(photon_t, x_atc),          1,  NULL, NATIVE_FLAGS},
    {"y_atc",           RecordObject::FLOAT,    offsetof(photon_t, y_atc),          1,  NULL, NATIVE_FLAGS},
    {"height",          RecordObject::FLOAT,    offsetof(photon_t, height),         1,  NULL, NATIVE_FLAGS},
    {"relief",          RecordObject::FLOAT,    offsetof(photon_t, relief),         1,  NULL, NATIVE_FLAGS},
    {"landcover",       RecordObject::UINT8,    offsetof(photon_t, landcover),      1,  NULL, NATIVE_FLAGS},
    {"snowcover",       RecordObject::UINT8,    offsetof(photon_t, snowcover),      1,  NULL, NATIVE_FLAGS},
    {"atl08_class",     RecordObject::UINT8,    offsetof(photon_t, atl08_class),    1,  NULL, NATIVE_FLAGS},
    {"atl03_cnf",       RecordObject::INT8,     offsetof(photon_t, atl03_cnf),      1,  NULL, NATIVE_FLAGS},
    {"quality_ph",      RecordObject::INT8,     offsetof(photon_t, quality_ph),     1,  NULL, NATIVE_FLAGS},
    {"yapc_score",      RecordObject::UINT8,    offsetof(photon_t, yapc_score),     1,  NULL, NATIVE_FLAGS}
};

const char* Atl03Reader::exRecType = "atl03rec";
const RecordObject::fieldDef_t Atl03Reader::exRecDef[] = {
    {"track",           RecordObject::UINT8,    offsetof(extent_t, track),                  1,  NULL, NATIVE_FLAGS},
    {"pair",            RecordObject::UINT8,    offsetof(extent_t, pair),                   1,  NULL, NATIVE_FLAGS},
    {"sc_orient",       RecordObject::UINT8,    offsetof(extent_t, spacecraft_orientation), 1,  NULL, NATIVE_FLAGS},
    {"rgt",             RecordObject::UINT16,   offsetof(extent_t, reference_ground_track), 1,  NULL, NATIVE_FLAGS},
    {"cycle",           RecordObject::UINT16,   offsetof(extent_t, cycle),                  1,  NULL, NATIVE_FLAGS},
    {"segment_id",      RecordObject::UINT32,   offsetof(extent_t, segment_id),             1,  NULL, NATIVE_FLAGS},
    {"segment_dist",    RecordObject::DOUBLE,   offsetof(extent_t, segment_distance),       1,  NULL, NATIVE_FLAGS}, // distance from equator
    {"background_rate", RecordObject::DOUBLE,   offsetof(extent_t, background_rate),        1,  NULL, NATIVE_FLAGS},
    {"solar_elevation", RecordObject::FLOAT,    offsetof(extent_t, solar_elevation),        1,  NULL, NATIVE_FLAGS},
    {"extent_id",       RecordObject::UINT64,   offsetof(extent_t, extent_id),              1,  NULL, NATIVE_FLAGS},
    {"photons",         RecordObject::USER,     offsetof(extent_t, photons),                0,  phRecType, NATIVE_FLAGS | RecordObject::BATCH} // variable length
};

const double Atl03Reader::ATL03_SEGMENT_LENGTH = 20.0; // meters

const char* Atl03Reader::OBJECT_TYPE = "Atl03Reader";
const char* Atl03Reader::LUA_META_NAME = "Atl03Reader";
const struct luaL_Reg Atl03Reader::LUA_META_TABLE[] = {
    {"parms",       luaParms},
    {"stats",       luaStats},
    {NULL,          NULL}
};

/******************************************************************************
 * ATL03 READER CLASS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * luaCreate - create(<asset>, <resource>, <outq_name>, <parms>, <send terminator>)
 *----------------------------------------------------------------------------*/
int Atl03Reader::luaCreate (lua_State* L)
{
    Asset* asset = NULL;
    Icesat2Parms* parms = NULL;

    try
    {
        /* Get Parameters */
        asset = dynamic_cast<Asset*>(getLuaObject(L, 1, Asset::OBJECT_TYPE));
        const char* resource = getLuaString(L, 2);
        const char* outq_name = getLuaString(L, 3);
        parms = dynamic_cast<Icesat2Parms*>(getLuaObject(L, 4, Icesat2Parms::OBJECT_TYPE));
        bool send_terminator = getLuaBoolean(L, 5, true, true);

        /* Return Reader Object */
        return createLuaObject(L, new Atl03Reader(L, asset, resource, outq_name, parms, send_terminator));
    }
    catch(const RunTimeException& e)
    {
        if(asset) asset->releaseLuaObject();
        if(parms) parms->releaseLuaObject();
        mlog(e.level(), "Error creating Atl03Reader: %s", e.what());
        return returnLuaStatus(L, false);
    }
}

/*----------------------------------------------------------------------------
 * init
 *----------------------------------------------------------------------------*/
void Atl03Reader::init (void)
{
    RECDEF(phRecType,       phRecDef,       sizeof(photon_t),       NULL);
    RECDEF(exRecType,       exRecDef,       sizeof(extent_t),       NULL /* "extent_id" */);
}

/*----------------------------------------------------------------------------
 * Constructor
 *----------------------------------------------------------------------------*/
Atl03Reader::Atl03Reader (lua_State* L, Asset* _asset, const char* _resource, const char* outq_name, Icesat2Parms* _parms, bool _send_terminator):
    LuaObject(L, OBJECT_TYPE, LUA_META_NAME, LUA_META_TABLE),
    read_timeout_ms(_parms->read_timeout * 1000)
{
    assert(_asset);
    assert(_resource);
    assert(outq_name);
    assert(_parms);

    /* Initialize Thread Count */
    threadCount = 0;

    /* Save Info */
    asset = _asset;
    resource = StringLib::duplicate(_resource);
    parms = _parms;

    /* Generate ATL08 Resource Name */
    resource08 = StringLib::duplicate(resource);
    resource08[4] = '8';

    /* Create Publisher */
    outQ = new Publisher(outq_name);
    sendTerminator = _send_terminator;

    /* Clear Statistics */
    stats.segments_read     = 0;
    stats.extents_filtered  = 0;
    stats.extents_sent      = 0;
    stats.extents_dropped   = 0;
    stats.extents_retried   = 0;

    /* Initialize Readers */
    active = true;
    numComplete = 0;
    memset(readerPid, 0, sizeof(readerPid));

    /* Set Thread Specific Trace ID for H5Coro */
    EventLib::stashId (traceId);

    /* Read Global Resource Information */
    try
    {
        /* Parse Globals (throws) */
        parseResource(resource, start_rgt, start_cycle, start_region);

        /* Create Readers */
        for(int track = 1; track <= Icesat2Parms::NUM_TRACKS; track++)
        {
            for(int pair = 0; pair < Icesat2Parms::NUM_PAIR_TRACKS; pair++)
            {
                if(parms->track == Icesat2Parms::ALL_TRACKS || track == parms->track)
                {
                    info_t* info = new info_t;
                    info->reader = this;
                    info->track = track;
                    info->pair = pair;
                    StringLib::format(info->prefix, 7, "/gt%d%c", info->track, info->pair == 0 ? 'l' : 'r');
                    readerPid[threadCount++] = new Thread(subsettingThread, info);
                }
            }
        }

        /* Check if Readers Created */
        if(threadCount == 0)
        {
            throw RunTimeException(CRITICAL, RTE_ERROR, "No reader threads were created, invalid track specified: %d\n", parms->track);
        }
    }
    catch(const RunTimeException& e)
    {
        /* Log Error */
        mlog(e.level(), "Failed to read global information in resource %s: %s", resource, e.what());

        /* Generate Exception Record */
        if(e.code() == RTE_TIMEOUT) LuaEndpoint::generateExceptionStatus(RTE_TIMEOUT, e.level(), outQ, &active, "%s: (%s)", e.what(), resource);
        else LuaEndpoint::generateExceptionStatus(RTE_RESOURCE_DOES_NOT_EXIST, e.level(), outQ, &active, "%s: (%s)", e.what(), resource);

        /* Indicate End of Data */
        if(sendTerminator) outQ->postCopy("", 0);
        signalComplete();
    }
}

/*----------------------------------------------------------------------------
 * Destructor
 *----------------------------------------------------------------------------*/
Atl03Reader::~Atl03Reader (void)
{
    active = false;

    for(int pid = 0; pid < threadCount; pid++)
    {
        delete readerPid[pid];
    }

    delete outQ;

    parms->releaseLuaObject();

    delete [] resource;
    delete [] resource08;

    asset->releaseLuaObject();
}

/*----------------------------------------------------------------------------
 * Region::Constructor
 *----------------------------------------------------------------------------*/
Atl03Reader::Region::Region (info_t* info):
    segment_lat    (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/reference_photon_lat").c_str(), &info->reader->context),
    segment_lon    (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/reference_photon_lon").c_str(), &info->reader->context),
    segment_ph_cnt (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/segment_ph_cnt").c_str(), &info->reader->context),
    inclusion_mask {NULL},
    inclusion_ptr  {NULL}
{
    try
    {
        /* Join Reads */
        segment_lat.join(info->reader->read_timeout_ms, true);
        segment_lon.join(info->reader->read_timeout_ms, true);
        segment_ph_cnt.join(info->reader->read_timeout_ms, true);

        /* Initialize Region */
        first_segment = 0;
        num_segments = H5Coro::ALL_ROWS;
        first_photon = 0;
        num_photons = H5Coro::ALL_ROWS;

        /* Determine Spatial Extent */
        if(info->reader->parms->raster != NULL)
        {
            rasterregion(info);
        }
        else if(info->reader->parms->points_in_poly > 0)
        {
            polyregion(info);
        }
        else
        {
            return; // early exit since no subsetting required
        }

        /* Check If Anything to Process */
        if(num_photons <= 0)
        {
            throw RunTimeException(DEBUG, RTE_EMPTY_SUBSET, "empty spatial region");
        }

        /* Trim Geospatial Extent Datasets Read from HDF5 File */
        segment_lat.trim(first_segment);
        segment_lon.trim(first_segment);
        segment_ph_cnt.trim(first_segment);
    }
    catch(const RunTimeException& e)
    {
        cleanup();
        throw;
    }

}

/*----------------------------------------------------------------------------
 * Region::Destructor
 *----------------------------------------------------------------------------*/
Atl03Reader::Region::~Region (void)
{
    cleanup();
}

/*----------------------------------------------------------------------------
 * Region::cleanup
 *----------------------------------------------------------------------------*/
void Atl03Reader::Region::cleanup (void)
{
    delete [] inclusion_mask;
    inclusion_mask = NULL;
}

/*----------------------------------------------------------------------------
 * Region::polyregion
 *----------------------------------------------------------------------------*/
void Atl03Reader::Region::polyregion (info_t* info)
{
    /* Find First Segment In Polygon */
    bool first_segment_found = false;
    int segment = 0;
    while(segment < segment_ph_cnt.size)
    {
        bool inclusion = false;

        /* Project Segment Coordinate */
        MathLib::coord_t segment_coord = {segment_lon[segment], segment_lat[segment]};
        MathLib::point_t segment_point = MathLib::coord2point(segment_coord, info->reader->parms->projection);

        /* Test Inclusion */
        if(MathLib::inpoly(info->reader->parms->projected_poly, info->reader->parms->points_in_poly, segment_point))
        {
            inclusion = true;
        }

        /* Check First Segment */
        if(!first_segment_found)
        {
            /* If Coordinate Is In Polygon */
            if(inclusion && segment_ph_cnt[segment] != 0)
            {
                /* Set First Segment */
                first_segment_found = true;
                first_segment = segment;

                /* Include Photons From First Segment */
                num_photons = segment_ph_cnt[segment];
            }
            else
            {
                /* Update Photon Index */
                first_photon += segment_ph_cnt[segment];
            }
        }
        else
        {
            /* If Coordinate Is NOT In Polygon */
            if(!inclusion && segment_ph_cnt[segment] != 0)
            {
                break; // full extent found!
            }

            /* Update Photon Index */
            num_photons += segment_ph_cnt[segment];
        }

        /* Bump Segment */
        segment++;
    }

    /* Set Number of Segments */
    if(first_segment_found)
    {
        num_segments = segment - first_segment;
    }
}

/*----------------------------------------------------------------------------
 * Region::rasterregion
 *----------------------------------------------------------------------------*/
void Atl03Reader::Region::rasterregion (info_t* info)
{
    /* Find First Segment In Polygon */
    bool first_segment_found = false;

    /* Check Size */
    if(segment_ph_cnt.size <= 0)
    {
        return;
    }

    /* Allocate Inclusion Mask */
    inclusion_mask = new bool [segment_ph_cnt.size];
    inclusion_ptr = inclusion_mask;

    /* Loop Throuh Segments */
    long curr_num_photons = 0;
    long last_segment = 0;
    int segment = 0;
    while(segment < segment_ph_cnt.size)
    {
        if(segment_ph_cnt[segment] != 0)
        {
            /* Check Inclusion */
            bool inclusion = info->reader->parms->raster->includes(segment_lon[segment], segment_lat[segment]);
            inclusion_mask[segment] = inclusion;

            /* Check For First Segment */
            if(!first_segment_found)
            {
                /* If Coordinate Is In Raster */
                if(inclusion)
                {
                    first_segment_found = true;

                    /* Set First Segment */
                    first_segment = segment;
                    last_segment = segment;

                    /* Include Photons From First Segment */
                    curr_num_photons = segment_ph_cnt[segment];
                    num_photons = curr_num_photons;
                }
                else
                {
                    /* Update Photon Index */
                    first_photon += segment_ph_cnt[segment];
                }
            }
            else
            {
                /* Update Photon Count and Segment */
                curr_num_photons += segment_ph_cnt[segment];

                /* If Coordinate Is In Raster */
                if(inclusion)
                {
                    /* Update Number of Photons to Current Count */
                    num_photons = curr_num_photons;

                    /* Update Number of Segments to Current Segment Count */
                    last_segment = segment;
                }
            }
        }

        /* Bump Segment */
        segment++;
    }

    /* Set Number of Segments */
    if(first_segment_found)
    {
        num_segments = last_segment - first_segment + 1;

        /* Trim Inclusion Mask */
        inclusion_ptr = &inclusion_mask[first_segment];
    }
}

/*----------------------------------------------------------------------------
 * Atl03Data::Constructor
 *----------------------------------------------------------------------------*/
Atl03Reader::Atl03Data::Atl03Data (info_t* info, const Region& region):
    sc_orient           (info->reader->asset, info->reader->resource,                                "/orbit_info/sc_orient",                &info->reader->context),
    velocity_sc         (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/velocity_sc").c_str(),     &info->reader->context, H5Coro::ALL_COLS, region.first_segment, region.num_segments),
    segment_delta_time  (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/delta_time").c_str(),      &info->reader->context, 0, region.first_segment, region.num_segments),
    segment_id          (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/segment_id").c_str(),      &info->reader->context, 0, region.first_segment, region.num_segments),
    segment_dist_x      (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/segment_dist_x").c_str(),  &info->reader->context, 0, region.first_segment, region.num_segments),
    solar_elevation     (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "geolocation/solar_elevation").c_str(), &info->reader->context, 0, region.first_segment, region.num_segments),
    dist_ph_along       (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/dist_ph_along").c_str(),       &info->reader->context, 0, region.first_photon,  region.num_photons),
    dist_ph_across      (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/dist_ph_across").c_str(),      &info->reader->context, 0, region.first_photon,  region.num_photons),
    h_ph                (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/h_ph").c_str(),                &info->reader->context, 0, region.first_photon,  region.num_photons),
    signal_conf_ph      (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/signal_conf_ph").c_str(),      &info->reader->context, info->reader->parms->surface_type, region.first_photon,  region.num_photons),
    quality_ph          (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/quality_ph").c_str(),          &info->reader->context, 0, region.first_photon,  region.num_photons),
    lat_ph              (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/lat_ph").c_str(),              &info->reader->context, 0, region.first_photon,  region.num_photons),
    lon_ph              (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/lon_ph").c_str(),              &info->reader->context, 0, region.first_photon,  region.num_photons),
    delta_time          (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "heights/delta_time").c_str(),          &info->reader->context, 0, region.first_photon,  region.num_photons),
    bckgrd_delta_time   (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "bckgrd_atlas/delta_time").c_str(),     &info->reader->context),
    bckgrd_rate         (info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, "bckgrd_atlas/bckgrd_rate").c_str(),    &info->reader->context),
    anc_geo_data        (NULL),
    anc_ph_data         (NULL)
{
    AncillaryFields::list_t* geo_fields = info->reader->parms->atl03_geo_fields;
    AncillaryFields::list_t* photon_fields = info->reader->parms->atl03_ph_fields;

    /* Read Ancillary Geolocation Fields */
    if(geo_fields)
    {
        anc_geo_data = new H5DArrayDictionary(Icesat2Parms::EXPECTED_NUM_FIELDS);
        for(int i = 0; i < geo_fields->length(); i++)
        {
            const char* field_name = (*geo_fields)[i].field.c_str();
            const char* group_name = "geolocation";
            if( (field_name[0] == 't' && field_name[1] == 'i' && field_name[2] == 'd') ||
                (field_name[0] == 'g' && field_name[1] == 'e' && field_name[2] == 'o') ||
                (field_name[0] == 'd' && field_name[1] == 'e' && field_name[2] == 'm') ||
                (field_name[0] == 'd' && field_name[1] == 'a' && field_name[2] == 'c') )
            {
                group_name = "geophys_corr";
            }
            FString dataset_name("%s/%s", group_name, field_name);
            H5DArray* array = new H5DArray(info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, dataset_name.c_str()).c_str(), &info->reader->context, 0, region.first_segment, region.num_segments);
            bool status = anc_geo_data->add(field_name, array);
            if(!status) delete array;
            assert(status); // the dictionary add should never fail
        }
    }

    /* Read Ancillary Photon Fields */
    if(photon_fields)
    {
        anc_ph_data = new H5DArrayDictionary(Icesat2Parms::EXPECTED_NUM_FIELDS);
        for(int i = 0; i < photon_fields->length(); i++)
        {
            const char* field_name = (*photon_fields)[i].field.c_str();
            FString dataset_name("heights/%s", field_name);
            H5DArray* array = new H5DArray(info->reader->asset, info->reader->resource, FString("%s/%s", info->prefix, dataset_name.c_str()).c_str(), &info->reader->context, 0, region.first_photon,  region.num_photons);
            bool status = anc_ph_data->add(field_name, array);
            if(!status) delete array;
            assert(status); // the dictionary add should never fail
        }
    }

    /* Join Hardcoded Reads */
    sc_orient.join(info->reader->read_timeout_ms, true);
    velocity_sc.join(info->reader->read_timeout_ms, true);
    segment_delta_time.join(info->reader->read_timeout_ms, true);
    segment_id.join(info->reader->read_timeout_ms, true);
    segment_dist_x.join(info->reader->read_timeout_ms, true);
    solar_elevation.join(info->reader->read_timeout_ms, true);
    dist_ph_along.join(info->reader->read_timeout_ms, true);
    dist_ph_across.join(info->reader->read_timeout_ms, true);
    h_ph.join(info->reader->read_timeout_ms, true);
    signal_conf_ph.join(info->reader->read_timeout_ms, true);
    quality_ph.join(info->reader->read_timeout_ms, true);
    lat_ph.join(info->reader->read_timeout_ms, true);
    lon_ph.join(info->reader->read_timeout_ms, true);
    delta_time.join(info->reader->read_timeout_ms, true);
    bckgrd_delta_time.join(info->reader->read_timeout_ms, true);
    bckgrd_rate.join(info->reader->read_timeout_ms, true);

    /* Join Ancillary Geolocation Reads */
    if(anc_geo_data)
    {
        H5DArray* array = NULL;
        const char* dataset_name = anc_geo_data->first(&array);
        while(dataset_name != NULL)
        {
            array->join(info->reader->read_timeout_ms, true);
            dataset_name = anc_geo_data->next(&array);
        }
    }

    /* Join Ancillary Photon Reads */
    if(anc_ph_data)
    {
        H5DArray* array = NULL;
        const char* dataset_name = anc_ph_data->first(&array);
        while(dataset_name != NULL)
        {
            array->join(info->reader->read_timeout_ms, true);
            dataset_name = anc_ph_data->next(&array);
        }
    }
}

/*----------------------------------------------------------------------------
 * Atl03Data::Destructor
 *----------------------------------------------------------------------------*/
Atl03Reader::Atl03Data::~Atl03Data (void)
{
    delete anc_geo_data;
    delete anc_ph_data;
}

/*----------------------------------------------------------------------------
 * Atl08Class::Constructor
 *----------------------------------------------------------------------------*/
Atl03Reader::Atl08Class::Atl08Class (info_t* info):
    enabled             (info->reader->parms->stages[Icesat2Parms::STAGE_ATL08]),
    phoreal             (info->reader->parms->stages[Icesat2Parms::STAGE_PHOREAL]),
    ancillary           (info->reader->parms->atl08_fields != NULL),
    classification      {NULL},
    relief              {NULL},
    landcover           {NULL},
    snowcover           {NULL},
    atl08_segment_id    (enabled ? info->reader->asset : NULL, info->reader->resource08, FString("%s/%s", info->prefix, "signal_photons/ph_segment_id").c_str(),       &info->reader->context08),
    atl08_pc_indx       (enabled ? info->reader->asset : NULL, info->reader->resource08, FString("%s/%s", info->prefix, "signal_photons/classed_pc_indx").c_str(),     &info->reader->context08),
    atl08_pc_flag       (enabled ? info->reader->asset : NULL, info->reader->resource08, FString("%s/%s", info->prefix, "signal_photons/classed_pc_flag").c_str(),     &info->reader->context08),
    atl08_ph_h          (phoreal ? info->reader->asset : NULL, info->reader->resource08, FString("%s/%s", info->prefix, "signal_photons/ph_h").c_str(),                &info->reader->context08),
    segment_id_beg      ((phoreal || ancillary) ? info->reader->asset : NULL, info->reader->resource08, FString("%s/%s", info->prefix, "land_segments/segment_id_beg").c_str(),       &info->reader->context08),
    segment_landcover   (phoreal ? info->reader->asset : NULL, info->reader->resource08, FString("%s/%s", info->prefix, "land_segments/segment_landcover").c_str(),    &info->reader->context08),
    segment_snowcover   (phoreal ? info->reader->asset : NULL, info->reader->resource08, FString("%s/%s", info->prefix, "land_segments/segment_snowcover").c_str(),    &info->reader->context08),
    anc_seg_data        (NULL),
    anc_seg_indices     (NULL)
{
    if(ancillary)
    {
        /* Allocate Ancillary Data Dictionary */
        anc_seg_data = new H5DArrayDictionary(Icesat2Parms::EXPECTED_NUM_FIELDS);
    
        /* Read Ancillary Fields */
        AncillaryFields::list_t* atl08_fields = info->reader->parms->atl08_fields;
        for(int i = 0; i < atl08_fields->length(); i++)
        {
            const char* field_name = (*atl08_fields)[i].field.c_str();
            FString dataset_name("%s/land_segments/%s", info->prefix, field_name);
            H5DArray* array = new H5DArray(info->reader->asset, info->reader->resource08, dataset_name.c_str(), &info->reader->context08);
            bool status = anc_seg_data->add(field_name, array);
            if(!status) delete array;
            assert(status); // the dictionary add should never fail
        }

        /* Join Ancillary Reads */
        H5DArray* array = NULL;
        const char* dataset_name = anc_seg_data->first(&array);
        while(dataset_name != NULL)
        {
            array->join(info->reader->read_timeout_ms, true);
            dataset_name = anc_seg_data->next(&array);
        }
    }
}

/*----------------------------------------------------------------------------
 * Atl08Class::Destructor
 *----------------------------------------------------------------------------*/
Atl03Reader::Atl08Class::~Atl08Class (void)
{
    delete [] classification;
    delete [] relief;
    delete [] landcover;
    delete [] snowcover;
    delete anc_seg_data;
    delete [] anc_seg_indices;
}

/*----------------------------------------------------------------------------
 * Atl08Class::classify
 *----------------------------------------------------------------------------*/
void Atl03Reader::Atl08Class::classify (info_t* info, const Region& region, const Atl03Data& atl03)
{
    /* Do Nothing If Not Enabled */
    if(!info->reader->parms->stages[Icesat2Parms::STAGE_ATL08])
    {
        return;
    }

    /* Wait for Reads to Complete */
    atl08_segment_id.join(info->reader->read_timeout_ms, true);
    atl08_pc_indx.join(info->reader->read_timeout_ms, true);
    atl08_pc_flag.join(info->reader->read_timeout_ms, true);
    if(phoreal || ancillary)
    {
        segment_id_beg.join(info->reader->read_timeout_ms, true);
    }
    if(phoreal)
    {
        atl08_ph_h.join(info->reader->read_timeout_ms, true);
        segment_landcover.join(info->reader->read_timeout_ms, true);
        segment_snowcover.join(info->reader->read_timeout_ms, true);
    }

    /* Allocate ATL08 Classification Array */
    int num_photons = atl03.dist_ph_along.size;
    classification = new uint8_t [num_photons];

    /* Allocate PhoREAL Arrays */
    if(phoreal)
    {
        relief = new float [num_photons];
        landcover = new uint8_t [num_photons];
        snowcover = new uint8_t [num_photons];
    }

    if(ancillary)
    {
        anc_seg_indices = new int32_t [num_photons];
    }

    /* Populate ATL08 Classifications */
    int32_t atl03_photon = 0;
    int32_t atl08_photon = 0;
    int32_t atl08_segment_index = 0;
    for(int atl03_segment_index = 0; atl03_segment_index < atl03.segment_id.size; atl03_segment_index++)
    {
        int32_t atl03_segment = atl03.segment_id[atl03_segment_index];

        /* Get Land and Snow Flags */
        if(phoreal || ancillary)
        {
            while( (atl08_segment_index < segment_id_beg.size) &&
                   ((segment_id_beg[atl08_segment_index] + NUM_ATL03_SEGS_IN_ATL08_SEG) <= atl03_segment) )
            {
                atl08_segment_index++;
            }
        }

        /* Get Per Photon Values */
        int32_t atl03_segment_count = region.segment_ph_cnt[atl03_segment_index];
        for(int atl03_count = 1; atl03_count <= atl03_segment_count; atl03_count++)
        {
            /* Go To Segment */
            while( (atl08_photon < atl08_segment_id.size) && // atl08 photon is valid
                   (atl08_segment_id[atl08_photon] < atl03_segment) )
            {
                atl08_photon++;
            }

            while( (atl08_photon < atl08_segment_id.size) && // atl08 photon is valid
                   (atl08_segment_id[atl08_photon] == atl03_segment) &&
                   (atl08_pc_indx[atl08_photon] < atl03_count))
            {
                atl08_photon++;
            }

            /* Check Match */
            if( (atl08_photon < atl08_segment_id.size) &&
                (atl08_segment_id[atl08_photon] == atl03_segment) &&
                (atl08_pc_indx[atl08_photon] == atl03_count) )
            {
                /* Assign Classification */
                classification[atl03_photon] = (uint8_t)atl08_pc_flag[atl08_photon];

                /* Populate PhoREAL Fields */
                if(phoreal)
                {
                    relief[atl03_photon] = atl08_ph_h[atl08_photon];
                    landcover[atl03_photon] = (uint8_t)segment_landcover[atl08_segment_index];
                    snowcover[atl03_photon] = (uint8_t)segment_snowcover[atl08_segment_index];

                    /* Run ABoVE Classifier (if specified) */
                    if(info->reader->parms->phoreal.above_classifier && (classification[atl03_photon] != Icesat2Parms::ATL08_TOP_OF_CANOPY))
                    {
                        uint8_t spot = Icesat2Parms::getSpotNumber((Icesat2Parms::sc_orient_t)atl03.sc_orient[0], (Icesat2Parms::track_t)info->track, info->pair);
                        if( (atl03.solar_elevation[atl03_segment] <= 5.0) &&
                            ((spot == 1) || (spot == 3) || (spot == 5)) &&
                            (atl03.signal_conf_ph[atl03_photon] == Icesat2Parms::CNF_SURFACE_HIGH) &&
                            ((relief[atl03_photon] >= 0.0) && (relief[atl03_photon] < 35.0)) )
                            /* TODO: check for valid ground photons in ATL08 segment */
                        {
                            /* Reassign Classification */
                            classification[atl03_photon] = Icesat2Parms::ATL08_TOP_OF_CANOPY;
                        }
                    }
                }

                /* Populate Ancillary Index */
                if(ancillary)
                {
                    anc_seg_indices[atl03_photon] = atl08_segment_index;
                }

                /* Go To Next ATL08 Photon */
                atl08_photon++;
            }
            else
            {
                /* Unclassified */
                classification[atl03_photon] = Icesat2Parms::ATL08_UNCLASSIFIED;

                /* Set PhoREAL Fields to Invalid */
                if(phoreal)
                {
                    relief[atl03_photon] = 0.0;
                    landcover[atl03_photon] = INVALID_FLAG;
                    snowcover[atl03_photon] = INVALID_FLAG;
                }

                /* Set Ancillary Index to Invalid */
                if(ancillary)
                {
                    anc_seg_indices[atl03_photon] = Atl03Reader::INVALID_INDICE;
                }
            }

            /* Go To Next ATL03 Photon */
            atl03_photon++;
        }
    }
}

/*----------------------------------------------------------------------------
 * Atl08Class::operator[]
 *----------------------------------------------------------------------------*/
uint8_t Atl03Reader::Atl08Class::operator[] (int index) const
{
    return classification[index];
}

/*----------------------------------------------------------------------------
 * YapcScore::Constructor
 *----------------------------------------------------------------------------*/
Atl03Reader::YapcScore::YapcScore (info_t* info, const Region& region, const Atl03Data& atl03):
    score {NULL}
{
    /* Do Nothing If Not Enabled */
    if(!info->reader->parms->stages[Icesat2Parms::STAGE_YAPC])
    {
        return;
    }

    /* Run YAPC */
    if(info->reader->parms->yapc.version == 3)
    {
        yapcV3(info, region, atl03);
    }
    else if(info->reader->parms->yapc.version == 2 || info->reader->parms->yapc.version == 1)
    {
        yapcV2(info, region, atl03);
    }
    else
    {
        throw RunTimeException(CRITICAL, RTE_ERROR, "Invalid YAPC version specified: %d", info->reader->parms->yapc.version);
    }
}

/*----------------------------------------------------------------------------
 * YapcScore::Destructor
 *----------------------------------------------------------------------------*/
Atl03Reader::YapcScore::~YapcScore (void)
{
    delete [] score;
}

/*----------------------------------------------------------------------------
 * yapcV2
 *----------------------------------------------------------------------------*/
void Atl03Reader::YapcScore::yapcV2 (info_t* info, const Region& region, const Atl03Data& atl03)
{
    /* YAPC Hard-Coded Parameters */
    const double MAXIMUM_HSPREAD = 15000.0; // meters
    const double HSPREAD_BINSIZE = 1.0; // meters
    const int MAX_KNN = 25;
    double nearest_neighbors[MAX_KNN];

    /* Shortcut to Settings */
    Icesat2Parms::yapc_t* settings = &info->reader->parms->yapc;

    /* Score Photons
     *
     *   CANNOT THROW BELOW THIS POINT
     */

    /* Allocate ATL08 Classification Array */
    int32_t num_photons = atl03.dist_ph_along.size;
    score = new uint8_t [num_photons];
    memset(score, 0, num_photons);

    /* Initialize Indices */
    int32_t ph_b0 = 0; // buffer start
    int32_t ph_b1 = 0; // buffer end
    int32_t ph_c0 = 0; // center start
    int32_t ph_c1 = 0; // center end

    /* Loop Through Each ATL03 Segment */
    int32_t num_segments = atl03.segment_id.size;
    for(int segment_index = 0; segment_index < num_segments; segment_index++)
    {
        /* Determine Indices */
        ph_b0 += segment_index > 1 ? region.segment_ph_cnt[segment_index - 2] : 0; // Center - 2
        ph_c0 += segment_index > 0 ? region.segment_ph_cnt[segment_index - 1] : 0; // Center - 1
        ph_c1 += region.segment_ph_cnt[segment_index]; // Center
        ph_b1 += segment_index < (num_segments - 1) ? region.segment_ph_cnt[segment_index + 1] : 0; // Center + 1

        /* Calculate N and KNN */
        int32_t N = region.segment_ph_cnt[segment_index];
        int knn = (settings->knn != 0) ? settings->knn : MAX(1, (sqrt((double)N) + 0.5) / 2);
        knn = MIN(knn, MAX_KNN); // truncate if too large

        /* Check Valid Extent (note check against knn)*/
        if((N <= knn) || (N < info->reader->parms->minimum_photon_count)) continue;

        /* Calculate Distance and Height Spread */
        double min_h = atl03.h_ph[0];
        double max_h = min_h;
        double min_x = atl03.dist_ph_along[0];
        double max_x = min_x;
        for(int n = 1; n < N; n++)
        {
            double h = atl03.h_ph[n];
            double x = atl03.dist_ph_along[n];
            if(h < min_h) min_h = h;
            if(h > max_h) max_h = h;
            if(x < min_x) min_x = x;
            if(x > max_x) max_x = x;
        }
        double hspread = max_h - min_h;
        double xspread = max_x - min_x;

        /* Check Window */
        if(hspread <= 0.0 || hspread > MAXIMUM_HSPREAD || xspread <= 0.0)
        {
            mlog(ERROR, "Unable to perform YAPC selection due to invalid photon spread: %lf, %lf\n", hspread, xspread);
            continue;
        }

        /* Bin Photons to Calculate Height Span*/
        int num_bins = (int)(hspread / HSPREAD_BINSIZE) + 1;
        int8_t* bins = new int8_t [num_bins];
        memset(bins, 0, num_bins);
        for(int n = 0; n < N; n++)
        {
            unsigned int bin = (unsigned int)((atl03.h_ph[n] - min_h) / HSPREAD_BINSIZE);
            bins[bin] = 1; // mark that photon present
        }

        /* Determine Number of Bins with Photons to Calculate Height Span
        * (and remove potential gaps in telemetry bands) */
        int nonzero_bins = 0;
        for(int b = 0; b < num_bins; b++) nonzero_bins += bins[b];
        delete [] bins;

        /* Calculate Height Span */
        double h_span = (nonzero_bins * HSPREAD_BINSIZE) / (double)N * (double)knn;

        /* Calculate Window Parameters */
        double half_win_x = settings->win_x / 2.0;
        double half_win_h = (settings->win_h != 0.0) ? settings->win_h / 2.0 : h_span / 2.0;

        /* Calculate YAPC Score for all Photons in Center Segment */
        for(int y = ph_c0; y < ph_c1; y++)
        {
            double smallest_nearest_neighbor = DBL_MAX;
            int smallest_nearest_neighbor_index = 0;
            int num_nearest_neighbors = 0;

            /* For All Neighbors */
            for(int x = ph_b0; x < ph_b1; x++)
            {
                /* Check for Identity */
                if(y == x) continue;

                /* Check Window */
                double delta_x = abs(atl03.dist_ph_along[x] - atl03.dist_ph_along[y]);
                if(delta_x > half_win_x) continue;

                /*  Calculate Weighted Distance */
                double delta_h = abs(atl03.h_ph[x] - atl03.h_ph[y]);
                double proximity = half_win_h - delta_h;

                /* Add to Nearest Neighbor */
                if(num_nearest_neighbors < knn)
                {
                    /* Maintain Smallest Nearest Neighbor */
                    if(proximity < smallest_nearest_neighbor)
                    {
                        smallest_nearest_neighbor = proximity;
                        smallest_nearest_neighbor_index = num_nearest_neighbors;
                    }

                    /* Automatically Add Nearest Neighbor (filling up array) */
                    nearest_neighbors[num_nearest_neighbors] = proximity;
                    num_nearest_neighbors++;
                }
                else if(proximity > smallest_nearest_neighbor)
                {
                    /* Add New Nearest Neighbor (replace current largest) */
                    nearest_neighbors[smallest_nearest_neighbor_index] = proximity;
                    smallest_nearest_neighbor = proximity; // temporarily set

                    /* Recalculate Largest Nearest Neighbor */
                    for(int k = 0; k < knn; k++)
                    {
                        if(nearest_neighbors[k] < smallest_nearest_neighbor)
                        {
                            smallest_nearest_neighbor = nearest_neighbors[k];
                            smallest_nearest_neighbor_index = k;
                        }
                    }
                }
            }

            /* Fill In Rest of Nearest Neighbors (if not already full) */
            for(int k = num_nearest_neighbors; k < knn; k++)
            {
                nearest_neighbors[k] = 0.0;
            }

            /* Calculate Inverse Sum of Distances from Nearest Neighbors */
            double nearest_neighbor_sum = 0.0;
            for(int k = 0; k < knn; k++)
            {
                if(nearest_neighbors[k] > 0.0)
                {
                    nearest_neighbor_sum += nearest_neighbors[k];
                }
            }
            nearest_neighbor_sum /= (double)knn;

            /* Calculate YAPC Score of Photon */
            score[y] = (uint8_t)((nearest_neighbor_sum / half_win_h) * 0xFF);
        }
    }
}

/*----------------------------------------------------------------------------
 * yapcV3
 *----------------------------------------------------------------------------*/
void Atl03Reader::YapcScore::yapcV3 (info_t* info, const Region& region, const Atl03Data& atl03)
{
    /* YAPC Parameters */
    Icesat2Parms::yapc_t* settings = &info->reader->parms->yapc;
    const double hWX = settings->win_x / 2; // meters
    const double hWZ = settings->win_h / 2; // meters

    /* Score Photons
     *
     *   CANNOT THROW BELOW THIS POINT
     */

    /* Allocate Photon Arrays */
    int32_t num_segments = atl03.segment_id.size;
    int32_t num_photons = atl03.dist_ph_along.size;
    score = new uint8_t [num_photons]; // class member freed in deconstructor
    double* ph_dist = new double[num_photons]; // local array freed below

    /* Populate Distance Array */
    int32_t ph_index = 0;
    for(int segment_index = 0; segment_index < num_segments; segment_index++)
    {
        for(int32_t ph_in_seg_index = 0; ph_in_seg_index < region.segment_ph_cnt[segment_index]; ph_in_seg_index++)
        {
            ph_dist[ph_index] = atl03.segment_dist_x[segment_index] + atl03.dist_ph_along[ph_index];
            ph_index++;
        }
    }

    /* Traverse Each Segment */
    ph_index = 0;
    for(int segment_index = 0; segment_index < num_segments; segment_index++)
    {
        /* Initialize Segment Parameters */
        int32_t N = region.segment_ph_cnt[segment_index];
        double* ph_weights = new double[N]; // local array freed below
        int max_knn = settings->min_knn;
        int32_t start_ph_index = ph_index;

        /* Traverse Each Photon in Segment*/
        for(int32_t ph_in_seg_index = 0; ph_in_seg_index < N; ph_in_seg_index++)
        {
            List<double> proximities;

            /* Check Nearest Neighbors to Left */
            int32_t neighbor_index = ph_index - 1;
            while(neighbor_index >= 0)
            {
                /* Check Inside Horizontal Window */
                double x_dist = ph_dist[ph_index] - ph_dist[neighbor_index];
                if(x_dist <= hWX)
                {
                    /* Check Inside Vertical Window */
                    double proximity = abs(atl03.h_ph[ph_index] - atl03.h_ph[neighbor_index]);
                    if(proximity <= hWZ)
                    {
                        proximities.add(proximity);
                    }
                }

                /* Check for Stopping Condition: 1m Buffer Added to X Window */
                if(x_dist >= (hWX + 1.0)) break;

                /* Goto Next Neighor */
                neighbor_index--;
            }

            /* Check Nearest Neighbors to Right */
            neighbor_index = ph_index + 1;
            while(neighbor_index < num_photons)
            {
                /* Check Inside Horizontal Window */
                double x_dist = ph_dist[neighbor_index] - ph_dist[ph_index];
                if(x_dist <= hWX)
                {
                    /* Check Inside Vertical Window */
                    double proximity = abs(atl03.h_ph[ph_index] - atl03.h_ph[neighbor_index]);
                    if(proximity <= hWZ) // inside of height window
                    {
                        proximities.add(proximity);
                    }
                }

                /* Check for Stopping Condition: 1m Buffer Added to X Window */
                if(x_dist >= (hWX + 1.0)) break;

                /* Goto Next Neighor */
                neighbor_index++;
            }

            /* Sort Proximities */
            proximities.sort();

            /* Calculate knn */
            double n = sqrt(proximities.length());
            int knn = MAX(n, settings->min_knn);
            if(knn > max_knn) max_knn = knn;

            /* Calculate Sum of Weights*/
            int num_nearest_neighbors = MIN(knn, proximities.length());
            double weight_sum = 0.0;
            for(int i = 0; i < num_nearest_neighbors; i++)
            {
                weight_sum += hWZ - proximities[i];
            }
            ph_weights[ph_in_seg_index] = weight_sum;

            /* Go To Next Photon */
            ph_index++;
        }

        /* Normalize Weights */
        for(int32_t ph_in_seg_index = 0; ph_in_seg_index < N; ph_in_seg_index++)
        {
            double Wt = ph_weights[ph_in_seg_index] / (hWZ * max_knn);
            score[start_ph_index] = (uint8_t)(MIN(Wt * 255, 255));
            start_ph_index++;
        }

        /* Free Photon Weights Array */
        delete [] ph_weights;
    }

    /* Free Photon Distance Array */
    delete [] ph_dist;
}

/*----------------------------------------------------------------------------
 * YapcScore::operator[]
 *----------------------------------------------------------------------------*/
uint8_t Atl03Reader::YapcScore::operator[] (int index) const
{
    return score[index];
}

/*----------------------------------------------------------------------------
 * TrackState::Constructor
 *----------------------------------------------------------------------------*/
Atl03Reader::TrackState::TrackState (const Atl03Data& atl03)
{
    ph_in              = 0;
    seg_in             = 0;
    seg_ph             = 0;
    start_segment      = 0;
    start_distance     = atl03.segment_dist_x[0];
    seg_distance       = 0.0;
    start_seg_portion  = 0.0;
    track_complete     = false;
    bckgrd_in          = 0;
    extent_segment     = 0;
    extent_valid       = true;
    extent_length      = 0.0;
}

/*----------------------------------------------------------------------------
 * TrackState::Destructor
 *----------------------------------------------------------------------------*/
Atl03Reader::TrackState::~TrackState (void)
{
}

/*----------------------------------------------------------------------------
 * subsettingThread
 *----------------------------------------------------------------------------*/
void* Atl03Reader::subsettingThread (void* parm)
{
    /* Get Thread Info */
    info_t* info = (info_t*)parm;
    Atl03Reader* reader = info->reader;
    Icesat2Parms* parms = reader->parms;
    stats_t local_stats = {0, 0, 0, 0, 0};
    List<int32_t>* segment_indices = NULL;    // used for ancillary data
    List<int32_t>* photon_indices = NULL;     // used for ancillary data
    List<int32_t>* atl08_indices = NULL;      // used for ancillary data

    /* Start Trace */
    uint32_t trace_id = start_trace(INFO, reader->traceId, "atl03_subsetter", "{\"asset\":\"%s\", \"resource\":\"%s\", \"track\":%d}", info->reader->asset->getName(), info->reader->resource, info->track);
    EventLib::stashId (trace_id); // set thread specific trace id for H5Coro

    try
    {        
        /* Start Reading ATL08 Data */
        Atl08Class atl08(info);

        /* Subset to Region of Interest */
        Region region(info);

        /* Read ATL03 Datasets */
        Atl03Data atl03(info, region);

        /* Perform YAPC Scoring (if requested) */
        YapcScore yapc(info, region, atl03);

        /* Perform ATL08 Classification (if requested) */
        atl08.classify(info, region, atl03);

        /* Initialize Track State */
        TrackState state(atl03);

        /* Increment Read Statistics */
        local_stats.segments_read = region.segment_ph_cnt.size;

        /* Calculate Length of Extent in Meters (used for distance) */
        state.extent_length = parms->extent_length;
        if(parms->dist_in_seg) state.extent_length *= ATL03_SEGMENT_LENGTH;

        /* Initialize Extent Counter */
        uint32_t extent_counter = 0;

        /* Traverse All Photons In Dataset */
        while(reader->active && !state.track_complete)
        {
            /* Setup Variables for Extent */
            int32_t current_photon = state.ph_in;
            int32_t current_segment = state.seg_in;
            int32_t current_count = state.seg_ph; // number of photons in current segment already accounted for
            bool extent_complete = false;
            bool step_complete = false;

            /* Set Extent State */
            state.start_seg_portion = atl03.dist_ph_along[current_photon] / ATL03_SEGMENT_LENGTH;
            state.extent_segment = state.seg_in;
            state.extent_valid = true;
            state.extent_photons.clear();

            /* Ancillary Extent Fields */
            if(atl03.anc_geo_data)
            {
                if(segment_indices) segment_indices->clear();
                else                segment_indices = new List<int32_t>;
            }

            /* Ancillary Photon Fields */
            if(atl03.anc_ph_data)
            {
                if(photon_indices) photon_indices->clear();
                else               photon_indices = new List<int32_t>;
            }

            /* Ancillary ATL08 Fields */
            if(atl08.anc_seg_data)
            {
                if(atl08_indices) atl08_indices->clear();
                else              atl08_indices = new List<int32_t>;
            }

            /* Traverse Photons Until Desired Along Track Distance Reached */
            while(!extent_complete || !step_complete)
            {
                /* Go to Photon's Segment */
                current_count++;
                while((current_segment < region.segment_ph_cnt.size) &&
                        (current_count > region.segment_ph_cnt[current_segment]))
                {
                    current_count = 1; // reset photons in segment
                    current_segment++; // go to next segment
                }

                /* Check Current Segment */
                if(current_segment >= atl03.segment_dist_x.size)
                {
                    mlog(ERROR, "Photons with no segments are detected is %s/%d     %d %ld %ld!", info->reader->resource, info->track, current_segment, atl03.segment_dist_x.size, region.num_segments);
                    state.track_complete = true;
                    break;
                }

                /* Update Along Track Distance and Progress */
                double delta_distance = atl03.segment_dist_x[current_segment] - state.start_distance;
                double x_atc = delta_distance + atl03.dist_ph_along[current_photon];
                int32_t along_track_segments = current_segment - state.extent_segment;

                /* Set Next Extent's First Photon */
                if((!step_complete) &&
                    ((!parms->dist_in_seg && x_atc >= parms->extent_step) ||
                    (parms->dist_in_seg && along_track_segments >= (int32_t)parms->extent_step)))
                {
                    state.ph_in = current_photon;
                    state.seg_in = current_segment;
                    state.seg_ph = current_count - 1;
                    step_complete = true;
                }

                /* Check if Photon within Extent's Length */
                if((!parms->dist_in_seg && x_atc < parms->extent_length) ||
                    (parms->dist_in_seg && along_track_segments < parms->extent_length))
                {
                    do
                    {
                        /* Check and Set Signal Confidence Level */
                        int8_t atl03_cnf = atl03.signal_conf_ph[current_photon];
                        if(atl03_cnf < Icesat2Parms::CNF_POSSIBLE_TEP || atl03_cnf > Icesat2Parms::CNF_SURFACE_HIGH)
                        {
                            throw RunTimeException(CRITICAL, RTE_ERROR, "invalid atl03 signal confidence: %d", atl03_cnf);
                        }
                        if(!parms->atl03_cnf[atl03_cnf + Icesat2Parms::SIGNAL_CONF_OFFSET])
                        {
                            break;
                        }

                        /* Check and Set ATL03 Photon Quality Level */
                        int8_t quality_ph = atl03.quality_ph[current_photon];
                        if(quality_ph < Icesat2Parms::QUALITY_NOMINAL || quality_ph > Icesat2Parms::QUALITY_POSSIBLE_TEP)
                        {
                            throw RunTimeException(CRITICAL, RTE_ERROR, "invalid atl03 photon quality: %d", quality_ph);
                        }
                        if(!parms->quality_ph[quality_ph])
                        {
                            break;
                        }

                        /* Check and Set ATL08 Classification */
                        Icesat2Parms::atl08_classification_t atl08_class = Icesat2Parms::ATL08_UNCLASSIFIED;
                        if(atl08.classification)
                        {
                            atl08_class = (Icesat2Parms::atl08_classification_t)atl08[current_photon];
                            if(atl08_class < 0 || atl08_class >= Icesat2Parms::NUM_ATL08_CLASSES)
                            {
                                throw RunTimeException(CRITICAL, RTE_ERROR, "invalid atl08 classification: %d", atl08_class);
                            }
                            if(!parms->atl08_class[atl08_class])
                            {
                                break;
                            }
                        }

                        /* Check and Set YAPC Score */
                        uint8_t yapc_score = 0;
                        if(yapc.score)
                        {
                            yapc_score = yapc[current_photon];
                            if(yapc_score < parms->yapc.score)
                            {
                                break;
                            }
                        }

                        /* Check Region */
                        if(region.inclusion_ptr)
                        {
                            if(!region.inclusion_ptr[current_segment])
                            {
                                break;
                            }
                        }

                        /* Set PhoREAL Fields */
                        float relief = 0.0;
                        uint8_t landcover_flag = Atl08Class::INVALID_FLAG;
                        uint8_t snowcover_flag = Atl08Class::INVALID_FLAG;
                        if(atl08.phoreal)
                        {
                            /* Set Relief */
                            if(!parms->phoreal.use_abs_h)
                            {
                                relief = atl08.relief[current_photon];
                            }
                            else
                            {
                                relief = atl03.h_ph[current_photon];
                            }

                            /* Set Flags */
                            landcover_flag = atl08.landcover[current_photon];
                            snowcover_flag = atl08.snowcover[current_photon];
                        }

                        /* Add Photon to Extent */
                        photon_t ph = {
                            .time_ns = Icesat2Parms::deltatime2timestamp(atl03.delta_time[current_photon]),
                            .latitude = atl03.lat_ph[current_photon],
                            .longitude = atl03.lon_ph[current_photon],
                            .x_atc = (float)(x_atc - (state.extent_length / 2.0)),
                            .y_atc = atl03.dist_ph_across[current_photon],
                            .height = atl03.h_ph[current_photon],
                            .relief = relief,
                            .landcover = landcover_flag,
                            .snowcover = snowcover_flag,
                            .atl08_class = (uint8_t)atl08_class,
                            .atl03_cnf = (int8_t)atl03_cnf,
                            .quality_ph = (int8_t)quality_ph,
                            .yapc_score = yapc_score
                        };
                        state.extent_photons.add(ph);

                        /* Index Photon for Ancillary Fields */
                        if(segment_indices)
                        {
                            segment_indices->add(current_segment);
                        }

                        /* Index Photon for Ancillary Fields */
                        if(photon_indices)
                        {
                            photon_indices->add(current_photon);
                        }

                        /* Index ATL08 Segment for Photon for Ancillary Fields */
                        if(atl08_indices)
                        {
                            atl08_indices->add(atl08.anc_seg_indices[current_photon]);
                        }
                    } while(false);
                }
                else
                {
                    extent_complete = true;
                }

                /* Go to Next Photon */
                current_photon++;

                /* Check Current Photon */
                if(current_photon >= atl03.dist_ph_along.size)
                {
                    state.track_complete = true;
                    break;
                }
            }

            /* Save Off Segment Distance to Include in Extent Record */
            state.seg_distance = state.start_distance + (state.extent_length / 2.0);

            /* Add Step to Start Distance */
            if(!parms->dist_in_seg)
            {
                state.start_distance += parms->extent_step; // step start distance

                /* Apply Segment Distance Correction and Update Start Segment */
                while( ((state.start_segment + 1) < atl03.segment_dist_x.size) &&
                        (state.start_distance >= atl03.segment_dist_x[state.start_segment + 1]) )
                {
                    state.start_distance += atl03.segment_dist_x[state.start_segment + 1] - atl03.segment_dist_x[state.start_segment];
                    state.start_distance -= ATL03_SEGMENT_LENGTH;
                    state.start_segment++;
                }
            }
            else // distance in segments
            {
                int32_t next_segment = state.extent_segment + (int32_t)parms->extent_step;
                if(next_segment < atl03.segment_dist_x.size)
                {
                    state.start_distance = atl03.segment_dist_x[next_segment]; // set start distance to next extent's segment distance
                }
            }

            /* Check Photon Count */
            if(state.extent_photons.length() < parms->minimum_photon_count)
            {
                state.extent_valid = false;
            }

            /* Check Along Track Spread */
            if(state.extent_photons.length() > 1)
            {
                int32_t last = state.extent_photons.length() - 1;
                double along_track_spread = state.extent_photons[last].x_atc - state.extent_photons[0].x_atc;
                if(along_track_spread < parms->along_track_spread)
                {
                    state.extent_valid = false;
                }
            }

            /* Create Extent Record */
            if(state.extent_valid || parms->pass_invalid)
            {
                /* Generate Extent ID */
                uint64_t extent_id = Icesat2Parms::generateExtentId(reader->start_rgt, reader->start_cycle, reader->start_region, info->track, info->pair, extent_counter);

                /* Build Extent and Ancillary Records */
                vector<RecordObject*> rec_list;
                try
                {
                    int rec_total_size = 0;
                    reader->generateExtentRecord(extent_id, info, state, atl03, rec_list, rec_total_size);
                    Atl03Reader::generateAncillaryRecords(extent_id, parms->atl03_ph_fields, atl03.anc_ph_data, AncillaryFields::PHOTON_ANC_TYPE, photon_indices, rec_list, rec_total_size);
                    Atl03Reader::generateAncillaryRecords(extent_id, parms->atl03_geo_fields, atl03.anc_geo_data, AncillaryFields::EXTENT_ANC_TYPE, segment_indices, rec_list, rec_total_size);
                    Atl03Reader::generateAncillaryRecords(extent_id, parms->atl08_fields, atl08.anc_seg_data, AncillaryFields::ATL08_ANC_TYPE, atl08_indices, rec_list, rec_total_size);

                    /* Send Records */
                    if(rec_list.size() == 1)
                    {
                        reader->postRecord(*(rec_list[0]), local_stats);
                    }
                    else if(rec_list.size() > 1)
                    {
                        /* Send Container Record */
                        ContainerRecord container(rec_list.size(), rec_total_size);
                        for(size_t i = 0; i < rec_list.size(); i++)
                        {
                            container.addRecord(*(rec_list[i]));
                        }
                        reader->postRecord(container, local_stats);
                    }
                }
                catch(const RunTimeException& e)
                {
                    mlog(e.level(), "Error posting results for resource %s track %d: %s", info->reader->resource, info->track, e.what());
                    LuaEndpoint::generateExceptionStatus(e.code(), e.level(), reader->outQ, &reader->active, "%s: (%s)", e.what(), info->reader->resource);
                }

                /* Clean Up Records */
                for(auto& rec: rec_list)
                {
                    delete rec;
                }
            }
            else // neither pair in extent valid
            {
                local_stats.extents_filtered++;
            }

            /* Bump Extent Counter */
            extent_counter++;
        }
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Failure during processing of resource %s track %d: %s", info->reader->resource, info->track, e.what());
        LuaEndpoint::generateExceptionStatus(e.code(), e.level(), reader->outQ, &reader->active, "%s: (%s)", e.what(), info->reader->resource);
    }

    /* Handle Global Reader Updates */
    reader->threadMut.lock();
    {
        /* Update Statistics */
        reader->stats.segments_read += local_stats.segments_read;
        reader->stats.extents_filtered += local_stats.extents_filtered;
        reader->stats.extents_sent += local_stats.extents_sent;
        reader->stats.extents_dropped += local_stats.extents_dropped;
        reader->stats.extents_retried += local_stats.extents_retried;

        /* Count Completion */
        reader->numComplete++;
        if(reader->numComplete == reader->threadCount)
        {
            mlog(INFO, "Completed processing resource %s", info->reader->resource);

            /* Indicate End of Data */
            if(reader->sendTerminator) reader->outQ->postCopy("", 0);
            reader->signalComplete();
        }
    }
    reader->threadMut.unlock();

    /* Clean Up Indices */
    delete segment_indices;
    delete photon_indices;
    delete atl08_indices;

    /* Clean Up Info */
    delete info;

    /* Stop Trace */
    stop_trace(INFO, trace_id);

    /* Return */
    return NULL;
}

/*----------------------------------------------------------------------------
 * calculateBackground
 *----------------------------------------------------------------------------*/
double Atl03Reader::calculateBackground (TrackState& state, const Atl03Data& atl03)
{
    double background_rate = atl03.bckgrd_rate[atl03.bckgrd_rate.size - 1];
    while(state.bckgrd_in < atl03.bckgrd_rate.size)
    {
        double curr_bckgrd_time = atl03.bckgrd_delta_time[state.bckgrd_in];
        double segment_time = atl03.segment_delta_time[state.extent_segment];
        if(curr_bckgrd_time >= segment_time)
        {
            /* Interpolate Background Rate */
            if(state.bckgrd_in > 0)
            {
                double prev_bckgrd_time = atl03.bckgrd_delta_time[state.bckgrd_in - 1];
                double prev_bckgrd_rate = atl03.bckgrd_rate[state.bckgrd_in - 1];
                double curr_bckgrd_rate = atl03.bckgrd_rate[state.bckgrd_in];

                double bckgrd_run = curr_bckgrd_time - prev_bckgrd_time;
                double bckgrd_rise = curr_bckgrd_rate - prev_bckgrd_rate;
                double segment_to_bckgrd_delta = segment_time - prev_bckgrd_time;

                background_rate = ((bckgrd_rise / bckgrd_run) * segment_to_bckgrd_delta) + prev_bckgrd_rate;
            }
            else
            {
                /* Use First Background Rate (no interpolation) */
                background_rate = atl03.bckgrd_rate[0];
            }
            break;
        }

        /* Go To Next Background Rate */
        state.bckgrd_in++;
    }
    return background_rate;
}

/*----------------------------------------------------------------------------
 * calculateSegmentId
 *----------------------------------------------------------------------------*/
uint32_t Atl03Reader::calculateSegmentId (const TrackState& state, const Atl03Data& atl03)
{
    /* Calculate Segment ID (attempt to arrive at closest ATL06 segment ID represented by extent) */
    double atl06_segment_id = (double)atl03.segment_id[state.extent_segment]; // start with first segment in extent
    if(!parms->dist_in_seg)
    {
        atl06_segment_id += state.start_seg_portion; // add portion of first segment that first photon is included
        atl06_segment_id += (int)((parms->extent_length / ATL03_SEGMENT_LENGTH) / 2.0); // add half the length of the extent
    }
    else // dist_in_seg is true
    {
        atl06_segment_id += (int)(parms->extent_length / 2.0);
    }

    /* Round Up */
    return (uint32_t)(atl06_segment_id + 0.5);
}

/*----------------------------------------------------------------------------
 * generateExtentRecord
 *----------------------------------------------------------------------------*/
void Atl03Reader::generateExtentRecord (uint64_t extent_id, info_t* info, TrackState& state, const Atl03Data& atl03, vector<RecordObject*>& rec_list, int& total_size)
{
    /* Calculate Extent Record Size */
    int num_photons = state.extent_photons.length();
    int extent_bytes = offsetof(extent_t, photons) + (sizeof(photon_t) * num_photons);

    /* Allocate and Initialize Extent Record */
    RecordObject* record            = new RecordObject(exRecType, extent_bytes);
    extent_t* extent                = (extent_t*)record->getRecordData();
    extent->valid                   = state.extent_valid;
    extent->extent_id               = extent_id;
    extent->track                   = info->track;
    extent->pair                    = info->pair;
    extent->spacecraft_orientation  = atl03.sc_orient[0];
    extent->reference_ground_track  = start_rgt;
    extent->cycle                   = start_cycle;
    extent->segment_id              = calculateSegmentId(state, atl03);
    extent->segment_distance        = state.seg_distance;
    extent->extent_length           = state.extent_length;
    extent->background_rate         = calculateBackground(state, atl03);
    extent->solar_elevation         = atl03.solar_elevation[state.extent_segment];
    extent->photon_count            = state.extent_photons.length();

    /* Calculate Spacecraft Velocity */
    int32_t sc_v_offset = state.extent_segment * 3;
    double sc_v1 = atl03.velocity_sc[sc_v_offset + 0];
    double sc_v2 = atl03.velocity_sc[sc_v_offset + 1];
    double sc_v3 = atl03.velocity_sc[sc_v_offset + 2];
    double spacecraft_velocity = sqrt((sc_v1*sc_v1) + (sc_v2*sc_v2) + (sc_v3*sc_v3));
    extent->spacecraft_velocity  = (float)spacecraft_velocity;

    /* Populate Photons */
    for(int32_t p = 0; p < state.extent_photons.length(); p++)
    {
        extent->photons[p] = state.extent_photons[p];
    }

    /* Add Extent Record */
    total_size += record->getAllocatedMemory();
    rec_list.push_back(record);
}

/*----------------------------------------------------------------------------
 * generateAncillaryRecords
 *----------------------------------------------------------------------------*/
void Atl03Reader::generateAncillaryRecords (uint64_t extent_id, AncillaryFields::list_t* field_list, H5DArrayDictionary* field_dict, AncillaryFields::type_t type, List<int32_t>* indices, vector<RecordObject*>& rec_list, int& total_size)
{
    if(field_list && field_dict && indices)
    {
        for(int i = 0; i < field_list->length(); i++)
        {
            /* Get Data Array */
            H5DArray* array = (*field_dict)[(*field_list)[i].field.c_str()];

            /* Create Ancillary Record */
            int record_size =   offsetof(AncillaryFields::element_array_t, data) +
                                (array->elementSize() * indices->length());
            RecordObject* record = new RecordObject(AncillaryFields::ancElementRecType, record_size);
            AncillaryFields::element_array_t* data = (AncillaryFields::element_array_t*)record->getRecordData();

            /* Populate Ancillary Record */
            data->extent_id = extent_id;
            data->anc_type = type;
            data->field_index = i;
            data->data_type = array->elementType();
            data->num_elements = indices->length();

            /* Populate Ancillary Data */
            uint64_t bytes_written = 0;
            for(int p = 0; p < indices->length(); p++)
            {
                int index = indices->get(p);
                if(index != Atl03Reader::INVALID_INDICE)
                {
                    bytes_written += array->serialize(&data->data[bytes_written], index, 1);
                }
                else
                {
                    for(int b = 0; b < array->elementSize(); b++)
                    {
                        data->data[bytes_written++] = 0xFF;
                    }
                }
            }

            /* Add Ancillary Record */
            total_size += record->getAllocatedMemory();
            rec_list.push_back(record);
        }
    }
}

/*----------------------------------------------------------------------------
 * postRecord
 *----------------------------------------------------------------------------*/
void Atl03Reader::postRecord (RecordObject& record, stats_t& local_stats)
{
    uint8_t* rec_buf = NULL;
    int rec_bytes = record.serialize(&rec_buf, RecordObject::REFERENCE);
    int post_status = MsgQ::STATE_TIMEOUT;
    while(active && (post_status = outQ->postCopy(rec_buf, rec_bytes, SYS_TIMEOUT)) == MsgQ::STATE_TIMEOUT)
    {
        local_stats.extents_retried++;
    }

    /* Update Statistics */
    if(post_status > 0)
    {
        local_stats.extents_sent++;
    }
    else
    {
        mlog(ERROR, "Atl03 reader failed to post %s to stream %s: %d", record.getRecordType(), outQ->getName(), post_status);
        local_stats.extents_dropped++;
    }
}

/*----------------------------------------------------------------------------
 * parseResource
 *
 *  ATL0x_YYYYMMDDHHMMSS_ttttccrr_vvv_ee
 *      YYYY    - year
 *      MM      - month
 *      DD      - day
 *      HH      - hour
 *      MM      - minute
 *      SS      - second
 *      tttt    - reference ground track
 *      cc      - cycle
 *      rr      - region
 *      vvv     - version
 *      ee      - revision
 *----------------------------------------------------------------------------*/
void Atl03Reader::parseResource (const char* _resource, int32_t& rgt, int32_t& cycle, int32_t& region)
{
    if(StringLib::size(_resource) < 29)
    {
        rgt = 0;
        cycle = 0;
        region = 0;
        return; // early exit on error
    }

    long val;
    char rgt_str[5];
    rgt_str[0] = _resource[21];
    rgt_str[1] = _resource[22];
    rgt_str[2] = _resource[23];
    rgt_str[3] = _resource[24];
    rgt_str[4] = '\0';
    if(StringLib::str2long(rgt_str, &val, 10))
    {
        rgt = val;
    }
    else
    {
        throw RunTimeException(CRITICAL, RTE_ERROR, "Unable to parse RGT from resource %s: %s", _resource, rgt_str);
    }

    char cycle_str[3];
    cycle_str[0] = _resource[25];
    cycle_str[1] = _resource[26];
    cycle_str[2] = '\0';
    if(StringLib::str2long(cycle_str, &val, 10))
    {
        cycle = val;
    }
    else
    {
        throw RunTimeException(CRITICAL, RTE_ERROR, "Unable to parse Cycle from resource %s: %s", _resource, cycle_str);
    }

    char region_str[3];
    region_str[0] = _resource[27];
    region_str[1] = _resource[28];
    region_str[2] = '\0';
    if(StringLib::str2long(region_str, &val, 10))
    {
        region = val;
    }
    else
    {
        throw RunTimeException(CRITICAL, RTE_ERROR, "Unable to parse Region from resource %s: %s", _resource, region_str);
    }
}

/*----------------------------------------------------------------------------
 * luaParms - :parms() --> {<key>=<value>, ...} containing parameters
 *----------------------------------------------------------------------------*/
int Atl03Reader::luaParms (lua_State* L)
{
    bool status = false;
    int num_obj_to_return = 1;
    Atl03Reader* lua_obj = NULL;

    try
    {
        /* Get Self */
        lua_obj = dynamic_cast<Atl03Reader*>(getLuaSelf(L, 1));
    }
    catch(const RunTimeException& e)
    {
        return luaL_error(L, "method invoked from invalid object: %s", __FUNCTION__);
    }

    try
    {
        /* Create Parameter Table */
        lua_newtable(L);
        LuaEngine::setAttrInt(L, Icesat2Parms::SURFACE_TYPE,         lua_obj->parms->surface_type);
        LuaEngine::setAttrNum(L, Icesat2Parms::ALONG_TRACK_SPREAD,   lua_obj->parms->along_track_spread);
        LuaEngine::setAttrInt(L, Icesat2Parms::MIN_PHOTON_COUNT,     lua_obj->parms->minimum_photon_count);
        LuaEngine::setAttrNum(L, Icesat2Parms::EXTENT_LENGTH,        lua_obj->parms->extent_length);
        LuaEngine::setAttrNum(L, Icesat2Parms::EXTENT_STEP,          lua_obj->parms->extent_step);
        lua_pushstring(L, Icesat2Parms::ATL03_CNF);
        lua_newtable(L);
        for(int i = Icesat2Parms::CNF_POSSIBLE_TEP; i <= Icesat2Parms::CNF_SURFACE_HIGH; i++)
        {
            lua_pushboolean(L, lua_obj->parms->atl03_cnf[i + Icesat2Parms::SIGNAL_CONF_OFFSET]);
            lua_rawseti(L, -2, i);
        }
        lua_settable(L, -3);

        /* Set Success */
        status = true;
        num_obj_to_return = 2;
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error returning parameters %s: %s", lua_obj->getName(), e.what());
    }

    /* Return Status */
    return returnLuaStatus(L, status, num_obj_to_return);
}

/*----------------------------------------------------------------------------
 * luaStats - :stats(<with_clear>) --> {<key>=<value>, ...} containing statistics
 *----------------------------------------------------------------------------*/
int Atl03Reader::luaStats (lua_State* L)
{
    bool status = false;
    int num_obj_to_return = 1;
    Atl03Reader* lua_obj = NULL;

    try
    {
        /* Get Self */
        lua_obj = dynamic_cast<Atl03Reader*>(getLuaSelf(L, 1));
    }
    catch(const RunTimeException& e)
    {
        return luaL_error(L, "method invoked from invalid object: %s", __FUNCTION__);
    }

    try
    {
        /* Get Clear Parameter */
        bool with_clear = getLuaBoolean(L, 2, true, false);

        /* Create Statistics Table */
        lua_newtable(L);
        LuaEngine::setAttrInt(L, "read",        lua_obj->stats.segments_read);
        LuaEngine::setAttrInt(L, "filtered",    lua_obj->stats.extents_filtered);
        LuaEngine::setAttrInt(L, "sent",        lua_obj->stats.extents_sent);
        LuaEngine::setAttrInt(L, "dropped",     lua_obj->stats.extents_dropped);
        LuaEngine::setAttrInt(L, "retried",     lua_obj->stats.extents_retried);

        /* Clear if Requested */
        if(with_clear) memset(&lua_obj->stats, 0, sizeof(lua_obj->stats));

        /* Set Success */
        status = true;
        num_obj_to_return = 2;
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error returning stats %s: %s", lua_obj->getName(), e.what());
    }

    /* Return Status */
    return returnLuaStatus(L, status, num_obj_to_return);
}
