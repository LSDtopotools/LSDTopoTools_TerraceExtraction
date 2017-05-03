//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// terraces_swath_driver.cpp
// Extract information about terraces using a shapefile of the main stem channel.
//
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// Fiona Clubb
// University of Edinburgh
//
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include "../LSDRaster.hpp"
#include "../LSDSwathProfile.hpp"
#include "../LSDShapeTools.hpp"
#include "../LSDJunctionNetwork.hpp"
#include "../LSDTerrace.hpp"
#include "../LSDParameterParser.hpp"
#include "../LSDSpatialCSVReader.hpp"

int main (int nNumberofArgs,char *argv[])
{
	//start the clock
	clock_t begin = clock();

	//Test for correct input arguments
  if (nNumberofArgs!=3)
  {
    cout << "=========================================================" << endl;
    cout << "|| Welcome to the terrace swath tool!  	              ||" << endl;
    cout << "|| This program takes in a csv file with latitude and  ||" << endl;
		cout << "|| longitude and gets a swath profile along            ||" << endl;
		cout << "|| a channel between the points. It then extracts			||" << endl;
		cout << "|| the terraces along the channel using slope and		  ||" << endl;
		cout << "|| relief thresholds.																	||" << endl;
    cout << "|| This program was developed by                       ||" << endl;
    cout << "|| Fiona J. Clubb												              ||" << endl;
    cout << "||  at the University of Edinburgh                     ||" << endl;
    cout << "=========================================================" << endl;
    cout << "This program requires two inputs: " << endl;
    cout << "* First the path to the parameter file." << endl;
    cout << "* Second the name of the param file (see below)." << endl;
    cout << "---------------------------------------------------------" << endl;
    cout << "Then the command line argument will be, for example: " << endl;
    cout << "In linux:" << endl;
    cout << "./get_terraces.out /LSDTopoTools/Topographic_projects/Test_data/ LSDTT_terraces.param" << endl;
		cout << "For more information please see the documentation: " << endl;
		cout << "http://lsdtopotools.github.io/LSDTT_book/#_terraces" << endl;
    cout << "=========================================================" << endl;
    exit(EXIT_SUCCESS);
  }
  string path_name = argv[1];
  string f_name = argv[2];

	// maps for setting default parameters
	map<string,int> int_default_map;
	map<string,float> float_default_map;
	map<string,bool> bool_default_map;
	map<string,string> string_default_map;

	// set default int parameters
	int_default_map["Threshold_SO"] = 3;
	int_default_map["Relief lower percentile"] = 25;
	int_default_map["Relief upper percentile"] = 75;
	int_default_map["Slope lower percentile"] = 25;
	int_default_map["Slope upper percentile"] = 75;
	int_default_map["Min patch size"] = 1000;
	int_default_map["search_radius"] = 10;
	int_default_map["NormaliseToBaseline"] = 1;

	// set default float parameters
	float_default_map["surface_fitting_window_radius"] = 6;
	float_default_map["Min slope filling"] = 0.0001;
	float_default_map["QQ threshold"] = 0.005;
	float_default_map["HalfWidth"] = 500;

	// set default bool parameters
	bool_default_map["Filter topography"] = true;

	// set default string parameters
	string_default_map["coords_csv_file"] = "NULL";

	// Use the parameter parser to get the maps of the parameters required for the
	// analysis
	// load parameter parser object
	LSDParameterParser LSDPP(path_name,f_name);
	LSDPP.force_bil_extension();

	LSDPP.parse_all_parameters(float_default_map, int_default_map, bool_default_map,string_default_map);
	map<string,float> this_float_map = LSDPP.get_float_parameters();
	map<string,int> this_int_map = LSDPP.get_int_parameters();
	map<string,bool> this_bool_map = LSDPP.get_bool_parameters();
	map<string,string> this_string_map = LSDPP.get_string_parameters();

	// Now print the parameters for bug checking
	LSDPP.print_parameters();

	// location of the files
	string DATA_DIR =  LSDPP.get_read_path();
	string DEM_ID =  LSDPP.get_read_fname();
	string OUT_DIR = LSDPP.get_write_path();
	string OUT_ID = LSDPP.get_write_fname();
	string DEM_extension =  LSDPP.get_dem_read_extension();
	vector<string> boundary_conditions = LSDPP.get_boundary_conditions();
	string CHeads_file = LSDPP.get_CHeads_file();

	// some error checking
	if (CHeads_file.empty())
	{
		cout << "FATAL ERROR: I can't find your channel heads file. Check your spelling!! \n The parameter key needs to be 'channel heads fname'" << endl;
		exit(EXIT_SUCCESS);
	}
	if (this_string_map["coords_csv_file"] == "NULL")
	{
		cout << "FATAL ERROR: I can't find your coordinates file. Check your spelling!! \n The parameter key needs to be 'coords_csv_file'" << endl;
		exit(EXIT_SUCCESS);
	}

  cout << "starting the test run... here we go!" << endl;

	LSDRaster RasterTemplate;

	if(this_bool_map["Filter topography"])
	{
		 // load the DEM
		 cout << "Loading the DEM..." << endl;
		 LSDRaster load_DEM((DATA_DIR+DEM_ID), DEM_extension);
		 RasterTemplate = load_DEM;

		 // filter using Perona Malik
		 int timesteps = 50;
		 float percentile_for_lambda = 90;
		 float dt = 0.1;
		 RasterTemplate = RasterTemplate.PeronaMalikFilter(timesteps, percentile_for_lambda, dt);

		 // fill
		 RasterTemplate = RasterTemplate.fill(this_float_map["Min slope filling"]);
		 string fill_name = "_filtered";
		 RasterTemplate.write_raster((DATA_DIR+DEM_ID+fill_name), DEM_extension);
	}
	else
	{
		//don't do the filtering, just load the filled DEM
		LSDRaster load_DEM((DATA_DIR+DEM_ID+"_filtered"), DEM_extension);
		RasterTemplate = load_DEM;
	}

	cout << "\t Flow routing..." << endl;
	// get a flow info object
	LSDFlowInfo FlowInfo(boundary_conditions, RasterTemplate);

	cout << "\t Loading the sources" << endl;
	// calcualte the distance from outlet
	LSDRaster DistanceFromOutlet = FlowInfo.distance_from_outlet();
	// load the sources
	vector<int> sources = FlowInfo.Ingest_Channel_Heads((DATA_DIR+CHeads_file), 2);
	cout << "\t Got sources!" << endl;

	// now get the junction network
	LSDJunctionNetwork ChanNetwork(sources, FlowInfo);
  cout << "\t Got the channel network" << endl;

	// reading in the csv file with the lat long points
	cout << "\t Reading in the csv file" << endl;
	LSDSpatialCSVReader SwathPoints(RasterTemplate, DATA_DIR+this_string_map["coords_csv_file"]);
	vector<float> UTME;
	vector<float> UTMN;
	SwathPoints.get_x_and_y_from_latlong(UTME, UTMN);
	cout << "\t Got the x and y locations" << endl;
	string csv_outname = "_UTM_check.csv";
	SwathPoints.print_UTM_coords_to_csv(UTME, UTMN, (DATA_DIR+DEM_ID+csv_outname));

	// snap to nearest channel
	vector<int> valid_indices;
	vector<int> snapped_nodes;
	vector<int> snapped_JNs;
	ChanNetwork.snap_point_locations_to_channels(UTME, UTMN, this_int_map["search_radius"], this_int_map["Threshold_SO"], FlowInfo, valid_indices, snapped_nodes, snapped_JNs);

	cout << "The number of valid points is: " << int(valid_indices.size()) << endl;

	if (int(valid_indices.size()) == 2)
	{
		// get the channel between these points
		cout << "Got channel nodes: " << snapped_nodes[0] << ", " << snapped_nodes[1] << endl;
		LSDIndexChannel BaselineChannel(snapped_nodes[0], snapped_nodes[1], FlowInfo);
		vector<double> X_coords;
		vector<double> Y_coords;
		BaselineChannel.get_coordinates_of_channel_nodes(X_coords, Y_coords);

		// get the point data from the BaselineChannel
		PointData BaselinePoints = get_point_data_from_coordinates(X_coords, Y_coords);

	  cout << "\t Creating swath template" << endl;
	  LSDSwath TestSwath(BaselinePoints, RasterTemplate, this_float_map["HalfWidth"]);

		cout << "\n\t Getting raster from swath" << endl;
		LSDRaster SwathRaster = TestSwath.get_raster_from_swath_profile(RasterTemplate, this_int_map["NormaliseToBaseline"]);
		string swath_ext = "_swath_raster";
		SwathRaster.write_raster((DATA_DIR+DEM_ID+swath_ext), DEM_extension);

	  // get the slope
		cout << "\t Getting the slope" << endl;
	  vector<LSDRaster> surface_fitting;
	  LSDRaster Slope;
	  vector<int> raster_selection(8, 0);
	  raster_selection[1] = 1;             // this means you want the slope
	  surface_fitting = RasterTemplate.calculate_polyfit_surface_metrics(this_float_map["surface_fitting_window_radius"], raster_selection);
	  Slope = surface_fitting[1];

		float mask_threshold = 1.0;
		bool below = 0;
		// remove any stupid slope values
		LSDRaster Slope_new = Slope.mask_to_nodata_using_threshold(mask_threshold, below);

		// get the channel relief and slope threshold using quantile-quantile plots
		cout << "Getting channel relief threshold from QQ plots" << endl;
		string qq_fname = DATA_DIR+DEM_ID+"_qq_relief.txt";
		float relief_threshold_from_qq = SwathRaster.get_threshold_for_floodplain_QQ(qq_fname, this_float_map["QQ threshold"], this_int_map["Relief lower percentile"], this_int_map["Relief upper percentile"]);

		cout << "Getting slope threshold from QQ plots" << endl;
		string qq_slope = DATA_DIR+DEM_ID+"_qq_slope.txt";
		float slope_threshold_from_qq = Slope_new.get_threshold_for_floodplain_QQ(qq_slope, this_float_map["QQ threshold"], this_int_map["Slope lower percentile"], this_int_map["Slope upper percentile"]);

		cout << "Relief threshold: " << relief_threshold_from_qq << " Slope threshold: " << slope_threshold_from_qq << endl;

		// get the terrace pixels
		int RemoveChannelThreshold = 2;
		LSDTerrace Terraces(SwathRaster, Slope_new, ChanNetwork, FlowInfo, relief_threshold_from_qq, slope_threshold_from_qq, this_int_map["Min patch size"], this_int_map["Threshold_SO"], RemoveChannelThreshold);
		LSDIndexRaster ConnectedComponents = Terraces.print_ConnectedComponents_to_Raster();
		string CC_ext = "_terrace_IDs";
		ConnectedComponents.write_raster((DATA_DIR+DEM_ID+CC_ext), DEM_extension);

		cout << "\t Testing connected components" << endl;
		vector <vector <float> > CC_vector = TestSwath.get_connected_components_along_swath(ConnectedComponents, RasterTemplate, this_int_map["NormaliseToBaseline"]);

		// push back results to file for plotting
		ofstream output_file_CC;
		string output_fname = "_terrace_swath_plots.txt";
		output_file_CC.open((DATA_DIR+DEM_ID+output_fname).c_str());
		for (int i = 0; i < int(CC_vector[0].size()); ++i)
		{
			output_file_CC << CC_vector[0][i] << " " << CC_vector[1][i] << " " << CC_vector[2][i] << endl;
		}
		output_file_CC.close();

		// write raster of terrace elevations
		LSDRaster ChannelRelief = Terraces.get_Terraces_RasterValues(SwathRaster);
		string relief_ext = "_terrace_relief_final";
		ChannelRelief.write_raster((DATA_DIR+DEM_ID+relief_ext), DEM_extension);
	}
	else
	{
		cout << "I was unable to find a channel between those coordinates! Check your coordinates or increase the search radius." << endl;
	}

	// Done, check how long it took
	clock_t end = clock();
	float elapsed_secs = float(end - begin) / CLOCKS_PER_SEC;
	cout << "DONE, Time taken (secs): " << elapsed_secs << endl;
}
