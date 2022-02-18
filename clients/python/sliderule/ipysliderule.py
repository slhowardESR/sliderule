# Copyright (c) 2021, University of Washington
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the University of Washington nor the names of its
#    contributors may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY OF WASHINGTON AND CONTRIBUTORS
# “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY OF WASHINGTON OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import sys
import copy
import datetime
import numpy as np
import geopandas as gpd
import matplotlib.cm as cm
import matplotlib.colorbar
import matplotlib.pyplot as plt
import matplotlib.colors as colors
from traitlets.utils.bunch import Bunch
import sliderule.io

# imports with warnings if not present
try:
    import ipywidgets
except ModuleNotFoundError as e:
    sys.stderr.write("Warning: missing packages, some functions will throw an exception if called. (%s)\n" % (str(e)))
try:
    import tkinter.filedialog
except ModuleNotFoundError as e:
    sys.stderr.write("Warning: missing packages, some functions will throw an exception if called. (%s)\n" % (str(e)))
try:
    import IPython.display
except ModuleNotFoundError as e:
    sys.stderr.write("Warning: missing packages, some functions will throw an exception if called. (%s)\n" % (str(e)))

# imports that raise error if not present
try:
    import ipyleaflet
except ModuleNotFoundError as e:
    sys.stderr.write("Error: missing required packages. (%s)\n" % (str(e)))
    raise

try:
    import xyzservices
except ModuleNotFoundError as e:
    sys.stderr.write("Error: missing required packages. (%s)\n" % (str(e)))
    raise

class widgets:
    def __init__(self, **kwargs):
        # set default keyword options
        kwargs.setdefault('style', {})
        # set style
        self.style = copy.copy(kwargs['style'])

        # dropdown menu for setting asset
        self.asset = ipywidgets.Dropdown(
            options=['atlas-local', 'atlas-s3', 'nsidc-s3'],
            value='nsidc-s3',
            description='Asset:',
            disabled=False,
            style=self.style,
        )

        # dropdown menu for setting data release
        self.release = ipywidgets.Dropdown(
            options=['003', '004'],
            value='004',
            description='Release:',
            disabled=False,
            style=self.style,
        )

        # dropdown menu for setting surface type
        # 0-land, 1-ocean, 2-sea ice, 3-land ice, 4-inland water
        surface_type_options = [
            'Land',
            'Ocean',
            'Sea ice',
            'Land ice',
            'Inland water'
        ]
        self.surface_type = ipywidgets.Dropdown(
            options=surface_type_options,
            value='Land',
            description='Surface Type:',
            disabled=False,
            style=self.style,
        )

        # slider for setting length of ATL06-SR segment in meters
        self.length = ipywidgets.IntSlider(
            value=40,
            min=5,
            max=200,
            step=5,
            description='Length:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # slider for setting step distance for successive segments in meters
        self.step = ipywidgets.IntSlider(
            value=20,
            min=5,
            max=200,
            step=5,
            description='Step:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # slider for setting confidence level for PE selection
        # eventually would be good to switch this to a IntRangeSlider with value=[0,4]
        self.confidence = ipywidgets.IntSlider(
            value=4,
            min=0,
            max=4,
            step=1,
            description='Confidence:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # selection for land surface classifications
        land_options = [
            'atl08_noise',
            'atl08_ground',
            'atl08_canopy',
            'atl08_top_of_canopy',
            'atl08_unclassified'
        ]
        self.land_class = ipywidgets.SelectMultiple(
            options=land_options,
            description='Land Class:',
            disabled=False,
            style=self.style,
        )

        # selection for ATL03 quality flags
        quality_options = [
            'atl03_nominal',
            'atl03_possible_afterpulse',
            'atl03_possible_impulse_response',
            'atl03_possible_tep'
        ]
        self.quality = ipywidgets.SelectMultiple(
            value=['atl03_nominal'],
            options=quality_options,
            description='Quality:',
            disabled=False,
            style=self.style,
        )

        # slider for setting for YAPC kNN
        self.yapc_knn = ipywidgets.IntSlider(
            value=0,
            min=0,
            max=20,
            step=1,
            description='YAPC kNN:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # slider for setting for YAPC height window
        self.yapc_win_h = ipywidgets.FloatSlider(
            value=3.0,
            min=0.1,
            max=100,
            step=0.1,
            description='YAPC h window:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='0.1f',
            style=self.style,
        )

        # slider for setting for YAPC along-track distance window
        self.yapc_win_x = ipywidgets.FloatSlider(
            value=21.0,
            min=0.1,
            max=100,
            step=0.1,
            description='YAPC x window:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='0.1f',
            style=self.style,
        )

        # slider for setting for YAPC minimum photon events
        self.yapc_min_ph = ipywidgets.IntSlider(
            value=4,
            min=0,
            max=20,
            step=1,
            description='YAPC Minimum PE:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # slider for setting for YAPC weights for fit
        self.yapc_weight = ipywidgets.IntSlider(
            value=80,
            min=0,
            max=255,
            step=1,
            description='YAPC Weight:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # slider for setting maximum number of iterations
        # (not including initial least-squares-fit selection)
        self.iteration = ipywidgets.IntSlider(
            value=1,
            min=0,
            max=20,
            step=1,
            description='Iterations:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # slider for setting minimum along track spread
        self.spread = ipywidgets.FloatSlider(
            value=20,
            min=1,
            max=100,
            step=0.1,
            description='Spread:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='0.1f',
            style=self.style,
        )
        # slider for setting minimum photon event (PE) count
        self.count = ipywidgets.IntSlider(
            value=10,
            min=1,
            max=50,
            step=1,
            description='PE Count:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='d',
            style=self.style,
        )

        # slider for setting minimum height of PE window in meters
        self.window = ipywidgets.FloatSlider(
            value=3,
            min=0.5,
            max=10,
            step=0.1,
            description='Window:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='0.1f',
            style=self.style,
        )

        # slider for setting maximum robust dispersion in meters
        self.sigma = ipywidgets.FloatSlider(
            value=5,
            min=1,
            max=10,
            step=0.1,
            description='Sigma:',
            disabled=False,
            continuous_update=False,
            orientation='horizontal',
            readout=True,
            readout_format='0.1f',
            style=self.style,
        )

        # dropdown menu for setting map projection for polygons
        # Global: Web Mercator (EPSG:3857)
        # North: Alaska Polar Stereographic (EPSG:5936)
        # South: Polar Stereographic South (EPSG:3031)
        projection_list = ['Global','North','South']
        self.projection = ipywidgets.Dropdown(
            options=projection_list,
            value='Global',
            description='Projection:',
            disabled=False,
            style=self.style,
        )

        # dropdown menu for selecting variable to draw on map
        variable_list = ['h_mean', 'h_sigma', 'dh_fit_dx', 'dh_fit_dy',
            'rms_misfit', 'w_surface_window_final', 'delta_time',
            'cycle', 'rgt']
        self.variable = ipywidgets.Dropdown(
            options=variable_list,
            value='h_mean',
            description='Variable:',
            disabled=False,
            style=self.style,
        )

        # all listed colormaps in matplotlib version
        cmap_set = set(cm.datad.keys()) | set(cm.cmaps_listed.keys())
        # colormaps available in this program
        # (no reversed, qualitative or miscellaneous)
        self.cmaps_listed = {}
        self.cmaps_listed['Perceptually Uniform Sequential'] = [
            'viridis','plasma','inferno','magma','cividis']
        self.cmaps_listed['Sequential'] = ['Greys','Purples',
            'Blues','Greens','Oranges','Reds','YlOrBr','YlOrRd',
            'OrRd','PuRd','RdPu','BuPu','GnBu','PuBu','YlGnBu',
            'PuBuGn','BuGn','YlGn']
        self.cmaps_listed['Sequential (2)'] = ['binary','gist_yarg',
            'gist_gray','gray','bone','pink','spring','summer',
            'autumn','winter','cool','Wistia','hot','afmhot',
            'gist_heat','copper']
        self.cmaps_listed['Diverging'] = ['PiYG','PRGn','BrBG',
            'PuOr','RdGy','RdBu','RdYlBu','RdYlGn','Spectral',
            'coolwarm', 'bwr','seismic']
        self.cmaps_listed['Cyclic'] = ['twilight',
            'twilight_shifted','hsv']
        # create list of available colormaps in program
        cmap_list = []
        for val in self.cmaps_listed.values():
            cmap_list.extend(val)
        # reduce colormaps to available in program and matplotlib
        cmap_set &= set(cmap_list)
        # dropdown menu for setting colormap
        self.cmap = ipywidgets.Dropdown(
            options=sorted(cmap_set),
            value='viridis',
            description='Colormap:',
            disabled=False,
            style=self.style,
        )

        # Reverse the colormap
        self.reverse = ipywidgets.Checkbox(
            value=False,
            description='Reverse Colormap',
            disabled=False,
            style=self.style,
        )

        # button and label for output file selection
        self.file = copy.copy(self.filename)
        self.savebutton = ipywidgets.Button(
            description="Save As"
        )
        self.savelabel = ipywidgets.Text(
            value=self.file,
            disabled=False
        )
        # connect fileselect button with action
        self.savebutton.on_click(self.saveas_file)
        self.savelabel.observe(self.set_savefile)
        # create hbox of file selection
        if os.environ.get("DISPLAY"):
            self.filesaver = ipywidgets.HBox([
                self.savebutton,
                self.savelabel
            ])
        else:
            self.filesaver = copy.copy(self.savelabel)

        # button and label for input file selection
        self.loadbutton = ipywidgets.Button(
            description="File select"
        )
        self.loadlabel = ipywidgets.Text(
            value='',
            disabled=False
        )
        # connect fileselect button with action
        self.loadbutton.on_click(self.select_file)
        self.loadlabel.observe(self.set_loadfile)
        # create hbox of file selection
        if os.environ.get("DISPLAY"):
            self.fileloader = ipywidgets.HBox([
                self.loadbutton,
                self.loadlabel
            ])
        else:
            self.fileloader = copy.copy(self.loadlabel)

    def saveas_file(self, b):
        """function for file save
        """
        IPython.display.clear_output()
        root = tkinter.Tk()
        root.withdraw()
        root.call('wm', 'attributes', '.', '-topmost', True)
        filetypes = (("HDF5 file", "*.h5"),
            ("netCDF file", "*.nc"),
            ("All Files", "*.*"))
        b.files = tkinter.filedialog.asksaveasfilename(
            initialfile=self.file,
            defaultextension='h5',
            filetypes=filetypes)
        self.savelabel.value = b.files
        self.file = b.files
        return self

    def set_savefile(self, sender):
        self.file = self.savelabel.value

    def select_file(self, b):
        """function for file selection
        """
        IPython.display.clear_output()
        root = tkinter.Tk()
        root.withdraw()
        root.call('wm', 'attributes', '.', '-topmost', True)
        filetypes = (("HDF5 file", "*.h5"),
            ("netCDF file", "*.nc"),
            ("All Files", "*.*"))
        b.files = tkinter.filedialog.askopenfilename(
            defaultextension='h5',
            filetypes=filetypes,
            multiple=False)
        self.loadlabel.value = b.files
        self.file = b.files
        return self

    def set_loadfile(self, sender):
        self.file = self.loadlabel.value

    @property
    def filename(self):
        """default input and output file string
        """
        # get sliderule submission time
        now = datetime.datetime.now().strftime('%Y%m%d%H%M%S')
        args = (now, self.release.value)
        return "ATL06-SR_{0}_{1}.h5".format(*args)

    @property
    def format(self):
        """return the file format from file string
        """
        hdf = ('h5','hdf5','hdf')
        netcdf = ('nc','netcdf','nc3')
        if self.file.endswith(hdf):
            return 'hdf'
        elif self.file.endswith(netcdf):
            return 'netcdf'
        else:
            return ''

    @property
    def _r(self):
        cmap_reverse_flag = '_r' if self.reverse.value else ''
        return cmap_reverse_flag

    @property
    def colormap(self):
        return self.cmap.value + self._r

# define projections for ipyleaflet tiles
projections = Bunch(
    # Alaska Polar Stereographic (WGS84)
    EPSG5936=dict(
        name='EPSG5936',
        custom=True,
        proj4def="""+proj=stere +lat_0=90 +lat_ts=90 +lon_0=-150 +k=0.994
            +x_0=2000000 +y_0=2000000 +datum=WGS84 +units=m +no_defs""",
        origin=[-2.8567784109255e+07, 3.2567784109255e+07],
        resolutions=[
            238810.813354,
            119405.406677,
            59702.7033384999,
            29851.3516692501,
            14925.675834625,
            7462.83791731252,
            3731.41895865639,
            1865.70947932806,
            932.854739664032,
            466.427369832148,
            233.213684916074,
            116.60684245803701,
            58.30342122888621,
            29.151710614575396,
            14.5758553072877,
            7.28792765351156,
            3.64396382688807,
            1.82198191331174,
            0.910990956788164,
            0.45549547826179,
            0.227747739130895,
            0.113873869697739,
            0.05693693484887,
            0.028468467424435
        ],
        bounds=[
            [-2623285.8808999992907047,-2623285.8808999992907047],
            [6623285.8803000003099442,6623285.8803000003099442]
        ]
    )
    ,
    # Polar Stereographic South (WGS84)
    EPSG3031=dict(
        name='EPSG3031',
        custom=True,
        proj4def="""+proj=stere +lat_0=-90 +lat_ts=-71 +lon_0=0 +k=1
            +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs""",
        origin=[-3.06361E7, 3.0636099999999993E7],
        resolutions=[
            67733.46880027094,
            33866.73440013547,
            16933.367200067736,
            8466.683600033868,
            4233.341800016934,
            2116.670900008467,
            1058.3354500042335,
            529.1677250021168,
            264.5838625010584,
        ],
        bounds=[
            [-4524583.19363305,-4524449.487765655],
            [4524449.4877656475,4524583.193633042]
        ]
    )
)

# attributions for the different basemaps
glims_attribution = """
Imagery reproduced from GLIMS and NSIDC (2005, updated 2018):
Global Land Ice Measurements from Space glacier database. (doi:10.7265/N5V98602)
"""
esri_attribution = """
Tiles &copy; Esri &mdash; Esri, DeLorme, NAVTEQ, TomTom, Intermap, iPC,
USGS, FAO, NPS, NRCAN, GeoBase, Kadaster NL, Ordnance Survey, Esri Japan,
METI, Esri China (Hong Kong), and the GIS User Community
"""
noaa_attribution = """
Imagery provided by NOAA National Centers for Environmental Information (NCEI);
International Bathymetric Chart of the Southern Ocean (IBCSO);
General Bathymetric Chart of the Oceans (GEBCO).
"""

# define background ipyleaflet tiles
basemaps = {
    "Esri": {
        "ArcticOceanBase": {
            "name": 'Esri.ArcticOceanBase',
            "crs": projections.EPSG5936,
            "attribution": esri_attribution,
            "url": 'http://server.arcgisonline.com/ArcGIS/rest/services/Polar/Arctic_Ocean_Base/MapServer/tile/{z}/{y}/{x}'
        },
        "ArcticOceanReference": {
            "name": 'Esri.ArcticOceanReference',
            "crs": projections.EPSG5936,
            "attribution": esri_attribution,
            "url": 'http://server.arcgisonline.com/ArcGIS/rest/services/Polar/Arctic_Ocean_Reference/MapServer/tile/{z}/{y}/{x}'
        },
        "AntarcticBasemap": {
            "name": 'Esri.AntarcticBasemap',
            "crs": projections.EPSG3031,
            "attribution":noaa_attribution,
            "url": 'https://tiles.arcgis.com/tiles/C8EMgrsFcRFL6LrL/arcgis/rest/services/Antarctic_Basemap/MapServer/tile/{z}/{y}/{x}'
        }
    }
}

# define background ipyleaflet WMS layers
layers = Bunch(
    GLIMS = Bunch(
        glaciers = ipyleaflet.WMSLayer(
            attribution=glims_attribution,
            layers='GLIMS_GLACIERS',
            format='image/png',
            url='https://www.glims.org/mapservice'
        )
    )
)

# load basemap providers from dict
# https://github.com/geopandas/xyzservices/blob/main/xyzservices/lib.py
def _load_dict(data):
    providers = Bunch()
    for provider_name in data.keys():
        provider = data[provider_name]
        if "url" in provider.keys():
            providers[provider_name] = xyzservices.lib.TileProvider(provider)
        else:
            providers[provider_name] = Bunch(
                {i: xyzservices.lib.TileProvider(provider[i]) for i in provider.keys()}
            )
    return providers

# draw ipyleaflet map
class leaflet:
    def __init__(self, projection, **kwargs):
        # set default keyword arguments
        kwargs.setdefault('zoom',False)
        kwargs.setdefault('scale',False)
        kwargs.setdefault('cursor',True)
        kwargs.setdefault('center',(39,-108))
        kwargs.setdefault('color','green')
        providers = _load_dict(basemaps)
        # create basemap in projection
        if (projection == 'Global'):
            self.map = ipyleaflet.Map(center=kwargs['center'],
                zoom=9, max_zoom=15,
                basemap=ipyleaflet.basemaps.Esri.WorldTopoMap)
            self.map.add_layer(layers.GLIMS.glaciers)
        elif (projection == 'North'):
            self.map = ipyleaflet.Map(center=(90,0),
                zoom=5, max_zoom=24,
                basemap=providers.Esri.ArcticOceanBase,
                crs=projections.EPSG5936)
            self.map.add_layer(providers.Esri.ArcticOceanReference)
        elif (projection == 'South'):
            self.map = ipyleaflet.Map(center=(-90,0),
                zoom=2, max_zoom=9,
                basemap=providers.Esri.AntarcticBasemap,
                crs=projections.EPSG3031)
        # add control for zoom
        if kwargs['zoom']:
            zoom_slider = ipywidgets.IntSlider(description='Zoom level:',
                min=self.map.min_zoom, max=self.map.max_zoom, value=self.map.zoom)
            ipywidgets.jslink((zoom_slider, 'value'), (self.map, 'zoom'))
            zoom_control = ipyleaflet.WidgetControl(widget=zoom_slider,
                position='topright')
            self.map.add_control(zoom_control)
        # add scale bar
        if kwargs['scale']:
            scale_control = ipyleaflet.ScaleControl(position='topright')
            self.map.add_control(scale_control)
        # add label for cursor position
        if kwargs['cursor']:
            self.cursor = ipywidgets.Label()
            label_control = ipyleaflet.WidgetControl(widget=self.cursor,
                position='bottomright')
            self.map.add_control(label_control)
            # keep track of cursor position
            self.map.on_interaction(self.handle_interaction)
        # add control for drawing polygons or bounding boxes
        draw_control = ipyleaflet.DrawControl(polyline={},circlemarker={},
            edit=False)
        shapeOptions = {'color':kwargs['color'],'fill_color':kwargs['color']}
        draw_control.rectangle = dict(shapeOptions=shapeOptions,
            metric=['km','m'])
        draw_control.polygon = dict(shapeOptions=shapeOptions,
            allowIntersection=False,showArea=True,metric=['km','m'])
        # create regions
        self.regions = []
        draw_control.on_draw(self.handle_draw)
        self.map.add_control(draw_control)
        # initiate data and colorbars
        self.data = None
        self.colorbar = None

    # handle cursor movements for label
    def handle_interaction(self, **kwargs):
        if (kwargs.get('type') == 'mousemove'):
            lat,lon = kwargs.get('coordinates')
            lon = sliderule.io.wrap_longitudes(lon)
            self.cursor.value = u"""Latitude: {d[0]:8.4f}\u00B0,
                Longitude: {d[1]:8.4f}\u00B0""".format(d=[lat,lon])

    # keep track of rectangles and polygons drawn on map
    def handle_draw(self, obj, action, geo_json):
        lon,lat = np.transpose(geo_json['geometry']['coordinates'])
        lon = sliderule.io.wrap_longitudes(lon)
        cx,cy = sliderule.io.centroid(lon,lat)
        wind = sliderule.io.winding(lon,lat)
        # set winding to counter-clockwise
        if (wind > 0):
            lon = lon[::-1]
            lat = lat[::-1]
        # create sliderule region from list
        region = sliderule.io.to_region(lon,lat)
        # append coordinates to list
        if (action == 'created'):
            self.regions.append(region)
        elif (action == 'deleted'):
            self.regions.remove(region)
        # remove any prior instances of a data layer
        if (action == 'deleted') and self.data is not None:
            self.map.remove_layer(self.data)
        return self

    # add geodataframe data to leaflet map
    def GeoData(self, gdf, **kwargs):
        kwargs.setdefault('column_name', 'h_mean')
        kwargs.setdefault('cmap', 'viridis')
        kwargs.setdefault('vmin', None)
        kwargs.setdefault('vmax', None)
        kwargs.setdefault('radius', 1.0)
        kwargs.setdefault('fillOpacity', 0.5)
        kwargs.setdefault('weight', 3.0)
        kwargs.setdefault('stride', None)
        kwargs.setdefault('max_plot_points', 10000)
        kwargs.setdefault('colorbar', True)
        # remove any prior instances of a data layer
        if self.data is not None:
            self.map.remove_layer(self.data)
        if kwargs['stride'] is not None:
            stride = np.copy(kwargs['stride'])
        elif (gdf.shape[0] > kwargs['max_plot_points']):
            stride = int(gdf.shape[0]//kwargs['max_plot_points'])
        else:
            stride = 1
        # extract column from geodataframe
        column_name = copy.copy(kwargs['column_name'])
        column = gpd.GeoDataFrame(gdf[column_name][::stride],
            geometry=gdf['geometry'][::stride])
        # set colorbar limits to 2-98 percentile
        # if not using a defined plot range
        clim = gdf[column_name].quantile((0.02, 0.98)).values
        if kwargs['vmin'] is None:
            vmin = clim[0]
        else:
            vmin = np.copy(kwargs['vmin'])
        if kwargs['vmax'] is None:
            vmax = clim[-1]
        else:
            vmax = np.copy(kwargs['vmax'])
        # normalize data to be within vmin and vmax
        norm = colors.Normalize(vmin=vmin, vmax=vmax, clip=True)
        normalized = norm(column[column_name])
        # create HEX colors for each point in the dataframe
        column["__plottable_color"] = np.apply_along_axis(colors.to_hex, 1,
            cm.get_cmap(kwargs['cmap'], 256)(normalized))
        # leaflet map point style
        point_style= {key:kwargs[key] for key in ['radius','fillOpacity','weight']}
        # convert to GeoJSON object and add to map
        self.data = ipyleaflet.GeoJSON(data=column.__geo_interface__,
            point_style=point_style, style_callback=self.style_callback)
        self.map.add_layer(self.data)
        # add colorbar
        if kwargs['colorbar']:
            self.add_colorbar(column_name=column_name, cmap=kwargs['cmap'], norm=norm)

    # functional call for setting colors of each point
    def style_callback(self, x):
        return {
            "fillColor": x["properties"]["__plottable_color"],
            "color": x["properties"]["__plottable_color"],
        }

    # add colorbar widget to leaflet map
    def add_colorbar(self, **kwargs):
        kwargs.setdefault('column_name', 'h_mean')
        kwargs.setdefault('cmap', 'viridis')
        kwargs.setdefault('norm', None)
        kwargs.setdefault('alpha', 1.0)
        kwargs.setdefault('orientation', 'horizontal')
        kwargs.setdefault('position', 'topright')
        kwargs.setdefault('width', 6.0)
        kwargs.setdefault('height', 0.4)
        # remove any prior instances of a colorbar
        if self.colorbar is not None:
            self.map.remove_control(self.colorbar)
        # colormap for colorbar
        cmap = copy.copy(cm.get_cmap(kwargs['cmap']))
        # create matplotlib colorbar
        _, ax = plt.subplots(figsize=(kwargs['width'], kwargs['height']))
        cbar = matplotlib.colorbar.ColorbarBase(ax, cmap=cmap,
            norm=kwargs['norm'], alpha=kwargs['alpha'],
            orientation=kwargs['orientation'],
            label=kwargs['column_name'])
        cbar.solids.set_rasterized(True)
        cbar.ax.tick_params(which='both', width=1, direction='in')
        # create output widget
        output = ipywidgets.widgets.Output()
        self.colorbar = ipyleaflet.WidgetControl(widget=output,
            transparent_bg=True, position=kwargs['position'])
        with output:
            IPython.display.clear_output()
            plt.show()
        # add colorbar
        self.map.add_control(self.colorbar)
