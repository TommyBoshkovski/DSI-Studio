#include <QFileInfo>
#include <iostream>
#include <iterator>
#include <string>
#include "image/image.hpp"
#include "boost/program_options.hpp"
#include <boost/exception/diagnostic_information.hpp>
#include "tracking_static_link.h"
#include "tracking/region/Regions.h"
#include "libs/tracking/tract_model.hpp"
#include "libs/tracking/tracking_thread.hpp"
#include "libs/tracking/tracking_model.hpp"
#include "libs/gzip_interface.hpp"

namespace po = boost::program_options;

// test example
// --action=trk --source=./test/20100129_F026Y_WANFANGYUN.src.gz.odf8.f3rec.de0.dti.fib.gz --method=0 --fiber_count=5000

int trk(int ac, char *av[])
{
    std::ofstream out("log.txt");
    try{
    // options for fiber tracking
    po::options_description trk_desc("fiber tracking options");
    trk_desc.add_options()
    ("help", "help message")
    ("action", po::value<std::string>(), "rec:diffusion reconstruction trk:fiber tracking")
    ("source", po::value<std::string>(), "assign the .fib file name")
    ("method", po::value<int>()->default_value(0), "tracking methods (0:streamline, 1:rk4)")
    ("initial_dir", po::value<int>()->default_value(0), "initial direction (0:primary, 1:random 2:all directions)")
    ("interpolation", po::value<int>()->default_value(0), "interpolation methods (0:trilinear, 1:gaussian radial)")
    ("seed_plan", po::value<int>()->default_value(0), "seeding methods (0:subvoxel, 1:voxelwise)")
    ("thread_count", po::value<int>()->default_value(1), "number of thread (default:1)")
    ("output", po::value<std::string>(), "output file name")
    ("export", po::value<std::string>(), "export additional information (e.g. --export=stat,tdi)")
    ("roi", po::value<std::string>(), "file for ROI regions")
    ("roi2", po::value<std::string>(), "file for the second ROI regions")
    ("roi3", po::value<std::string>(), "file for the third ROI regions")
    ("roi4", po::value<std::string>(), "file for the forth ROI regions")
    ("roi5", po::value<std::string>(), "file for the fifth ROI regions")
    ("roa", po::value<std::string>(), "file for ROA regions")
    ("end", po::value<std::string>(), "file for ending regions")
    ("end2", po::value<std::string>(), "file for ending regions")
    ("seed", po::value<std::string>(), "file for seed regions")
    ("threshold_index", po::value<std::string>(), "index for thresholding")
    ("step_size", po::value<float>()->default_value(1), "the step size in minimeter (default:1)")
    ("turning_angle", po::value<float>()->default_value(60), "the turning angle in degrees (default:60)")
    ("fa_threshold", po::value<float>(), "the fa threshold (default:0.03)")
    ("smoothing", po::value<float>()->default_value(0), "smoothing fiber tracts, from 0 to 1. (default:0)")
    ("min_length", po::value<float>()->default_value(10), "minimum fiber length in minimeter (default:10)")
    ("max_length", po::value<float>()->default_value(500), "maximum fiber length in minimeter (default:500)")
    ("fiber_count", po::value<int>(), "terminate tracking if fiber count is reached (default:10000)")
    ("seed_count", po::value<int>(), "terminate tracking if seeding count is reached  (default:10000)")
    ;

    if(!ac)
    {
        out << trk_desc << std::endl;
        return 1;
    }

    po::variables_map vm;
    po::store(po::command_line_parser(ac, av).options(trk_desc).allow_unregistered().run(), vm);
    po::notify(vm);

    std::auto_ptr<ODFModel> handle(new ODFModel);
    {
        std::string file_name = vm["source"].as<std::string>();
        out << "loading " << file_name.c_str() << "..." <<std::endl;
        if(!QFileInfo(file_name.c_str()).exists())
        {
            out << file_name.c_str() << " does not exist. terminating..." << std::endl;
            return 0;
        }
        if (!handle->load_from_file(file_name.c_str()))
        {
            out << "Open file " << file_name.c_str() << " failed" << std::endl;
            out << "msg:" << handle->fib_data.error_msg.c_str() << std::endl;
            return 0;
        }
    }
    if (vm.count("threshold_index"))
    {
        out << "setting index to " << vm["threshold_index"].as<std::string>() << std::endl;
        if(!handle->fib_data.fib.set_tracking_index(vm["threshold_index"].as<std::string>()))
        {
            out << "failed...cannot find the index";
            return 0;
        }
    }




    image::geometry<3> geometry = handle->fib_data.dim;
    image::vector<3> voxel_size = handle->fib_data.vs;
    const float *fa0 = handle->fib_data.fib.fa[0];

    float param[8] = {1,60,60,0.03,0.0,10.0,500.0};
    param[0] = vm["step_size"].as<float>();
    param[2] = param[1] = vm["turning_angle"].as<float>();
    param[1] *= 3.1415926/180.0;
    param[2] *= 3.1415926/180.0;
    if (vm.count("fa_threshold") )
        param[3] = vm["fa_threshold"].as<float>();
    else
        param[3] = 0.6*image::segmentation::otsu_threshold(
                image::basic_image<float, 3,image::const_pointer_memory<float> >(fa0,geometry));
    param[4] = vm["smoothing"].as<float>();
    param[5] = vm["min_length"].as<float>();
    param[6] = vm["max_length"].as<float>();

    //set
    {
        out << "step_size=" << param[0] << std::endl;
        out << "turning_angle=" << param[1] << std::endl;
        out << "interpo_angle=" << param[2] << std::endl;
        out << "fa_threshold=" << param[3] << std::endl;
        out << "smoothing=" << param[4] << std::endl;
        out << "min_length=" << param[5] << std::endl;
        out << "max_length=" << param[6] << std::endl;
        out << "tracking_method=" << vm["method"].as<int>() << std::endl;
        out << "initial direction=" << vm["initial_dir"].as<int>() << std::endl;
        out << "interpolation=" << vm["interpolation"].as<int>() << std::endl;
        out << "thread_count=" << vm["thread_count"].as<int>() << std::endl;
    }

    bool stop_by_track = true;
    unsigned int termination_count = 10000;
    if (vm.count("fiber_count"))
    {
        termination_count = vm["fiber_count"].as<int>();
        stop_by_track = true;
    }        
    if (vm.count("seed_count"))
    {
        termination_count = vm["seed_count"].as<int>();
        stop_by_track = false;
    }
    out << (stop_by_track ? "fiber_count=" : "seed_count=") <<
            termination_count << std::endl;
    unsigned char methods[5];
    methods[0] = vm["method"].as<int>();
    methods[1] = vm["initial_dir"].as<int>();
    methods[2] = vm["interpolation"].as<int>();
    methods[3] = stop_by_track;
    methods[4] = vm["seed_plan"].as<int>();
    ThreadData tracking_thread;


    char rois[5][5] = {"roi","roi2","roi3","roi4","roi5"};
    for(int index = 0;index < 5;++index)
    if (vm.count(rois[index]))
    {
        ROIRegion roi(geometry, voxel_size);
        std::string file_name = vm[rois[index]].as<std::string>();
        if(!QFileInfo(file_name.c_str()).exists())
        {
            out << file_name.c_str() << " does not exist. terminating..." << std::endl;
            return 0;
        }
        if(!roi.LoadFromFile(file_name.c_str(),handle->fib_data.trans_to_mni))
        {
            out << "Invalid file format:" << file_name << std::endl;
            return 0;
        }
        tracking_thread.setRegions(geometry,roi.get(),0);
        out << rois[index] << "=" << file_name << std::endl;
    }

    if (vm.count("roa"))
    {
        ROIRegion roa(geometry, voxel_size);
        std::string file_name = vm["roa"].as<std::string>();
        if(!QFileInfo(file_name.c_str()).exists())
        {
            out << file_name.c_str() << " does not exist. terminating..." << std::endl;
            return 0;
        }
        if(!roa.LoadFromFile(file_name.c_str(),handle->fib_data.trans_to_mni))
        {
            out << "Invalid file format:" << file_name.c_str() << std::endl;
            return 0;
        }
        tracking_thread.setRegions(geometry,roa.get(),1);
        out << "roa=" << file_name << std::endl;
    }
    if (vm.count("end"))
    {
        ROIRegion end(geometry, voxel_size);
        std::string file_name = vm["end"].as<std::string>();
        if(!QFileInfo(file_name.c_str()).exists())
        {
            out << file_name.c_str() << " does not exist. terminating..." << std::endl;
            return 0;
        }
        if(!end.LoadFromFile(file_name.c_str(),handle->fib_data.trans_to_mni))
        {
            out << "Invalid file format:" << file_name.c_str() << std::endl;
            return 0;
        }
        tracking_thread.setRegions(geometry,end.get(),2);
        out << "end=" << file_name << std::endl;
    }
    if (vm.count("end2"))
    {
        ROIRegion end(geometry, voxel_size);
        std::string file_name = vm["end2"].as<std::string>();
        if(!QFileInfo(file_name.c_str()).exists())
        {
            out << file_name.c_str() << " does not exist. terminating..." << std::endl;
            return 0;
        }
        if(!end.LoadFromFile(file_name.c_str(),handle->fib_data.trans_to_mni))
        {
            out << "Invalid file format:" << file_name.c_str() << std::endl;
            return 0;
        }
        tracking_thread.setRegions(geometry,end.get(),2);
        out << "end2=" << file_name << std::endl;
    }
    if (vm.count("seed"))
    {
        ROIRegion seed(geometry, voxel_size);
        std::string file_name = vm["seed"].as<std::string>();
        if(!QFileInfo(file_name.c_str()).exists())
        {
            out << file_name.c_str() << " does not exist. terminating..." << std::endl;
            return 0;
        }
        if(!seed.LoadFromFile(file_name.c_str(),handle->fib_data.trans_to_mni))
        {
            out << "Invalid file format:" << file_name.c_str() << std::endl;
            return 0;
        }
        tracking_thread.setRegions(geometry,seed.get(),3);
        out << "seed=" << file_name << std::endl;
    }
    else
    {
        std::vector<image::vector<3,short> > seed;
        out << "no seeding area assigned. perform whole brain tracking" << std::endl;
        for(image::pixel_index<3> index;index.valid(geometry);index.next(geometry))
            if(fa0[index.index()])
                seed.push_back(image::vector<3,short>(index.x(),index.y(),index.z()));
        tracking_thread.setRegions(geometry,seed,3);
    }

    std::string file_name;
    if (vm.count("output"))
        file_name = vm["output"].as<std::string>();
    else
    {
        std::ostringstream fout;
        fout << vm["source"].as<std::string>() <<
            ".st" << (int)std::floor(param[0]*10.0+0.5) <<
            ".tu" << (int)std::floor(param[1]+0.5) <<
            ".in" << (int)std::floor(param[2]+0.5) <<
            ".fa" << (int)std::floor(param[3]*100.0+0.5) <<
            ".sm" << (int)std::floor(param[4]*10.0+0.5) <<
            ".me" << (int)vm["method"].as<int>() <<
            ".sd" << (int)vm["initial_dir"].as<int>() <<
            ".pd" << (int)vm["interpolation"].as<int>() <<
            ".trk";
        file_name = fout.str();
    }

    out << "start tracking..." << std::endl;
    std::auto_ptr<TractModel> tract_model(new TractModel(handle.get(),geometry,voxel_size));
    {
        tract_model->get_fib().threshold = param[3];
        tract_model->get_fib().cull_cos_angle = std::cos(param[1]);
        tracking_thread.run(tract_model->get_fib(),param,methods,vm["thread_count"].as<int>(),termination_count,true);
    }

    tracking_thread.fetchTracks(tract_model.get());
    out << "finished tracking." << std::endl;
    out << "output file:" << file_name << std::endl;
    tract_model->save_tracts_to_file(file_name.c_str());

    // export statistics
    if(vm.count("export") && vm["export"].as<std::string>().find("stat")!=std::string::npos)
    {
        out << "export statistics..." << std::endl;
        std::string file_name_stat(file_name);
        file_name_stat += ".stat.txt";
        std::ofstream out_stat(file_name_stat.c_str());
        std::string result;
        tract_model->get_quantitative_info(result);
        out_stat << result;
    }
    // export qa
    if(vm.count("export") && vm["export"].as<std::string>().find("qa")!=std::string::npos)
    {
        out << "export qa..." << std::endl;
        std::string file_name_stat(file_name);
        file_name_stat += ".qa.txt";
        tract_model->save_fa_to_file(file_name_stat.c_str());
    }

    // export tdi
    if(vm.count("export") && vm["export"].as<std::string>().find("tdi")!=std::string::npos)
    {
        out << "export tract density images..." << std::endl;
        std::string file_name_stat(file_name);
        file_name_stat += ".tdi.nii";
        image::basic_image<unsigned int,3> tdi(geometry);
        std::vector<float> tr(16);
        tr[0] = tr[5] = tr[10] = tr[15] = 1.0;
        tract_model->get_density_map(tdi,tr,false);

        gz_nifti nii_header;
        image::flip_xy(tdi);
        nii_header << tdi;
        nii_header.set_voxel_size(voxel_size.begin());
        nii_header.save_to_file(file_name_stat.c_str());
    }
    /*
    if (vm.count("endpoint"))
    {
        out << "output endpoint." << std::endl;
        file_name += ".end.";
        file_name += vm["endpoint"].as<std::string>();
        tract_model_save_end_points(tract_handle,file_name.c_str());
    }*/
    }
    catch(boost::exception const&  ex)
    {
        out << "program terminated due to exception:" <<
               boost::diagnostic_information(ex) << std::endl;
    }
    catch(std::exception const&  ex)
    {
        out << "program terminated due to exception:" << ex.what() << std::endl;
    }
    catch(...)
    {
        out << "program terminated due to unkown exception" << std::endl;
    }

}
