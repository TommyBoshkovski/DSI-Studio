#include <QFileInfo>
#include <QInputDialog>
#include "image_model.hpp"
#include "odf_process.hpp"
#include "dti_process.hpp"
#include "fib_data.hpp"

void ImageModel::draw_mask(tipl::color_image& buffer,int position)
{
    if (!dwi.size())
        return;
    buffer.resize(tipl::geometry<2>(dwi.width(),dwi.height()));
    unsigned int offset = position*buffer.size();
    std::copy(dwi.begin() + offset,
              dwi.begin()+ offset + buffer.size(),buffer.begin());

    unsigned char* slice_image_ptr = &*dwi.begin() + buffer.size()* position;
    unsigned char* slice_mask = &*voxel.mask.begin() + buffer.size()* position;

    tipl::color_image buffer2(tipl::geometry<2>(dwi.width()*2,dwi.height()));
    tipl::draw(buffer,buffer2,tipl::vector<2,int>());
    for (unsigned int index = 0; index < buffer.size(); ++index)
    {
        unsigned char value = slice_image_ptr[index];
        if (slice_mask[index])
            buffer[index] = tipl::rgb(255, value, value);
        else
            buffer[index] = tipl::rgb(value, value, value);
    }
    tipl::draw(buffer,buffer2,tipl::vector<2,int>(dwi.width(),0));
    buffer2.swap(buffer);
}

void ImageModel::calculate_dwi_sum(void)
{
    dwi_sum.clear();
    dwi_sum.resize(voxel.dim);
    tipl::par_for(dwi_sum.size(),[&](unsigned int pos)
    {
        for (unsigned int index = 0;index < src_dwi_data.size();++index)
            dwi_sum[pos] += src_dwi_data[index][pos];
    });

    float max_value = *std::max_element(dwi_sum.begin(),dwi_sum.end());
    float min_value = max_value;
    for (unsigned int index = 0;index < dwi_sum.size();++index)
        if (dwi_sum[index] < min_value && dwi_sum[index] > 0)
            min_value = dwi_sum[index];


    tipl::minus_constant(dwi_sum,min_value);
    tipl::lower_threshold(dwi_sum,0.0f);
    float t = tipl::segmentation::otsu_threshold(dwi_sum);
    tipl::upper_threshold(dwi_sum,t*3.0f);
    tipl::normalize(dwi_sum,1.0);

    // update dwi
    dwi.resize(voxel.dim);
    float min = tipl::minimum(dwi_sum);
    float range = tipl::maximum(dwi_sum)-min;
    float r = range > 0.0 ? 255.9f/range:1.0f;
    for(unsigned int index = 0;index < dwi.size();++index)
        dwi[index] = (dwi_sum[index]-min)*r;
}

void ImageModel::remove(unsigned int index)
{
    src_dwi_data.erase(src_dwi_data.begin()+index);
    src_bvalues.erase(src_bvalues.begin()+index);
    src_bvectors.erase(src_bvectors.begin()+index);
    shell.clear();
    voxel.dwi_data.clear();
}

typedef boost::mpl::vector<
    ReadDWIData,
    Dwi2Tensor
> check_btable_process;


void flip_fib_dir(std::vector<tipl::vector<3> >& fib_dir,const unsigned char* order)
{
    for(int j = 0;j < fib_dir.size();++j)
    {
        fib_dir[j] = tipl::vector<3>(fib_dir[j][order[0]],fib_dir[j][order[1]],fib_dir[j][order[2]]);
        if(order[3])
            fib_dir[j][0] = -fib_dir[j][0];
        if(order[4])
            fib_dir[j][1] = -fib_dir[j][1];
        if(order[5])
            fib_dir[j][2] = -fib_dir[j][2];
    }
}
void ImageModel::flip_b_table(const unsigned char* order)
{
    for(unsigned int index = 0;index < src_bvectors.size();++index)
    {
        float x = src_bvectors[index][order[0]];
        float y = src_bvectors[index][order[1]];
        float z = src_bvectors[index][order[2]];
        src_bvectors[index][0] = x;
        src_bvectors[index][1] = y;
        src_bvectors[index][2] = z;
        if(order[3])
            src_bvectors[index][0] = -src_bvectors[index][0];
        if(order[4])
            src_bvectors[index][1] = -src_bvectors[index][1];
        if(order[5])
            src_bvectors[index][2] = -src_bvectors[index][2];
    }
    voxel.grad_dev.clear();
}
void ImageModel::pre_dti(void)
{
    bool output_dif = voxel.output_diffusivity;
    bool output_tensor = voxel.output_tensor;
    voxel.output_diffusivity = true;
    voxel.output_tensor = false;
    reconstruct<check_btable_process>("Checking b-table");
    voxel.output_diffusivity = output_dif;
    voxel.output_tensor = output_tensor;
}

std::string ImageModel::check_b_table(void)
{
    set_title("Checking B-table");
    pre_dti();
    std::vector<tipl::image<float,3> > fib_fa(1);
    std::vector<std::vector<tipl::vector<3> > > fib_dir(1);
    fib_fa[0].swap(voxel.fib_fa);
    fib_dir[0].swap(voxel.fib_dir);

    const unsigned char order[24][6] = {
                            {0,1,2,0,0,0},
                            {0,1,2,1,0,0},
                            {0,1,2,0,1,0},
                            {0,1,2,0,0,1},
                            {0,2,1,0,0,0},
                            {0,2,1,1,0,0},
                            {0,2,1,0,1,0},
                            {0,2,1,0,0,1},
                            {1,0,2,0,0,0},
                            {1,0,2,1,0,0},
                            {1,0,2,0,1,0},
                            {1,0,2,0,0,1},
                            {1,2,0,0,0,0},
                            {1,2,0,1,0,0},
                            {1,2,0,0,1,0},
                            {1,2,0,0,0,1},
                            {2,1,0,0,0,0},
                            {2,1,0,1,0,0},
                            {2,1,0,0,1,0},
                            {2,1,0,0,0,1},
                            {2,0,1,0,0,0},
                            {2,0,1,1,0,0},
                            {2,0,1,0,1,0},
                            {2,0,1,0,0,1}};
    const char txt[24][7] = {".012",".012fx",".012fy",".012fz",
                             ".021",".021fx",".021fy",".021fz",
                             ".102",".102fx",".102fy",".102fz",
                             ".120",".120fx",".120fy",".120fz",
                             ".210",".210fx",".210fy",".210fz",
                             ".201",".201fx",".201fy",".201fz"};

    float result[24] = {0};
    float otsu = tipl::segmentation::otsu_threshold(fib_fa[0])*0.6;
    float cur_score = evaluate_fib(voxel.dim,otsu,fib_fa,[&](int pos,char fib){return fib_dir[fib][pos];}).first;
    result[0] = cur_score;
    for(int i = 1;i < 24;++i)// 0 is the current score
    {
        auto new_dir(fib_dir);
        flip_fib_dir(new_dir[0],order[i]);
        result[i] = evaluate_fib(voxel.dim,otsu,fib_fa,[&](int pos,char fib){return new_dir[fib][pos];}).first;
    }
    int best = std::max_element(result,result+24)-result;

    if(result[best] > cur_score)
    {
        std::cout << "b-table corrected by " << txt[best] << " for " << file_name << std::endl;
        flip_b_table(order[best]);
        voxel.load_from_src(*this);
        return txt[best];
    }
    fib_fa[0].swap(voxel.fib_fa);
    fib_dir[0].swap(voxel.fib_dir);

    return std::string();
}
std::vector<std::pair<int,int> > ImageModel::get_bad_slices(void)
{
    voxel.load_from_src(*this);
    std::vector<char> skip_slice(voxel.dim.depth());
    for(int i = 0,pos = 0;i < skip_slice.size();++i,pos += voxel.dim.plane_size())
        if(std::accumulate(voxel.mask.begin()+pos,voxel.mask.begin()+pos+voxel.dim.plane_size(),(int)0) < voxel.dim.plane_size()/16)
            skip_slice[i] = 1;
        else
            skip_slice[i] = 0
                    ;
    tipl::image<float,2> cor_values(tipl::geometry<2>(voxel.dwi_data.size(),voxel.dim.depth()));

    tipl::par_for(voxel.dwi_data.size(),[&](int index)
    {
        auto I = tipl::make_image(voxel.dwi_data[index],voxel.dim);
        int value_index = index*(voxel.dim.depth());
        for(int z = 0,pos = 0;z < voxel.dim.depth();++z,pos += voxel.dim.plane_size())
        {
            float cor = 0.0f;

            if(z)
                cor = tipl::correlation(&I[pos],&I[pos]+I.plane_size(),&I[pos]-I.plane_size());
            if(z+1 < voxel.dim.depth())
                cor = std::max<float>(cor,tipl::correlation(&I[pos],&I[pos]+I.plane_size(),&I[pos]+I.plane_size()));

            if(index-1 >= 0)
                cor = std::max<float>(cor,tipl::correlation(voxel.dwi_data[index]+pos,
                                        voxel.dwi_data[index]+pos+voxel.dim.plane_size(),
                                        voxel.dwi_data[index-1]+pos));
            if(index+1 < voxel.dwi_data.size())
                cor = std::max<float>(cor,tipl::correlation(voxel.dwi_data[index]+pos,
                                                            voxel.dwi_data[index]+pos+voxel.dim.plane_size(),
                                                            voxel.dwi_data[index+1]+pos));

            cor_values[value_index+z] = cor;
        }
    });
    // check the difference with neighborings
    std::vector<int> bad_i,bad_z;
    std::vector<float> sum;
    for(int i = 0,pos = 0;i < voxel.dwi_data.size();++i)
    {
        for(int z = 0;z < voxel.dim.depth();++z,++pos)
        if(!skip_slice[z])
        {
            // ignore the top and bottom slices
            if(z <= 1 || z + 2 >= voxel.dim.depth())
                continue;
            float v[4] = {0.0f,0.0f,0.0f,0.0f};
            if(z > 0)
                v[0] = cor_values[pos-1]-cor_values[pos];
            if(z+1 < voxel.dim.depth())
                v[1] = cor_values[pos+1]-cor_values[pos];
            if(i > 0)
                v[2] = cor_values[pos-voxel.dim.depth()]-cor_values[pos];
            if(i+1 < voxel.dwi_data.size())
                v[3] = cor_values[pos+voxel.dim.depth()]-cor_values[pos];
            float s = 0.0;
            s = v[0]+v[1]+v[2]+v[3];
            if(s > 0.4f)
            {
                bad_i.push_back(i);
                bad_z.push_back(z);
                sum.push_back(s);
            }
        }
    }

    std::vector<std::pair<int,int> > result;

    auto arg = tipl::arg_sort(sum,std::less<float>());
    //tipl::image<float,3> bad_I(tipl::geometry<3>(voxel.dim[0],voxel.dim[1],bad_i.size()));
    for(int i = 0,out_pos = 0;i < bad_i.size();++i,out_pos += voxel.dim.plane_size())
    {
        result.push_back(std::make_pair(bad_i[arg[i]],bad_z[arg[i]]));
        //int pos = bad_z[arg[i]]*voxel.dim.plane_size();
    //    std::copy(voxel.dwi_data[bad_i[arg[i]]]+pos,voxel.dwi_data[bad_i[arg[i]]]+pos+voxel.dim.plane_size(),bad_I.begin()+out_pos);
    }
    //bad_I.save_to_file<gz_nifti>("D:/bad.nii.gz");
    return result;
}

float ImageModel::quality_control_neighboring_dwi_corr(void)
{
    std::vector<std::pair<int,int> > corr_pairs;
    for(int i = 0;i < src_bvalues.size();++i)
    {
        if(src_bvalues[i] == 0.0f)
            continue;
        float min_dis = std::numeric_limits<float>::max();
        int min_j = 0;
        for(int j = i+1;j < src_bvalues.size();++j)
        {
            tipl::vector<3> v1(src_bvectors[i]),v2(src_bvectors[j]);
            v1 *= std::sqrt(src_bvalues[i]);
            v2 *= std::sqrt(src_bvalues[j]);
            float dis = std::min<float>((v1-v2).length(),(v1+v2).length());
            if(dis < min_dis)
            {
                min_dis = dis;
                min_j = j;
            }
        }
        corr_pairs.push_back(std::make_pair(i,min_j));
    }
    float self_cor = 0.0f;
    unsigned int count = 0;
    tipl::par_for(corr_pairs.size(),[&](int index)
    {
        int i1 = corr_pairs[index].first;
        int i2 = corr_pairs[index].second;
        std::vector<float> I1,I2;
        I1.reserve(voxel.dim.size());
        I2.reserve(voxel.dim.size());
        for(int i = 0;i < voxel.dim.size();++i)
            if(voxel.mask[i])
            {
                I1.push_back(src_dwi_data[i1][i]);
                I2.push_back(src_dwi_data[i2][i]);
            }
        self_cor += tipl::correlation(I1.begin(),I1.end(),I2.begin());
        ++count;
    });
    self_cor/= (float)count;
    return self_cor;
}
bool ImageModel::is_human_data(void) const
{
    return voxel.dim[0]*voxel.vs[0] > 150 && voxel.dim[1]*voxel.vs[1] > 150;
}

bool ImageModel::command(std::string cmd,std::string param)
{
    if(cmd == "[Step T2a][Erosion]")
    {
        tipl::morphology::erosion(voxel.mask);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2a][Dilation]")
    {
        tipl::morphology::dilation(voxel.mask);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2a][Defragment]")
    {
        tipl::morphology::defragment(voxel.mask);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2a][Smoothing]")
    {
        tipl::morphology::smoothing(voxel.mask);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2a][Negate]")
    {
        tipl::morphology::negate(voxel.mask);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2a][Threshold]")
    {
        int threshold;
        if(param.empty())
        {
            bool ok;
            threshold = QInputDialog::getInt(0,"DSI Studio","Please assign the threshold",
                                                 (int)tipl::segmentation::otsu_threshold(dwi),
                                                 (int)*std::min_element(dwi.begin(),dwi.end()),
                                                 (int)*std::max_element(dwi.begin(),dwi.end())+1,1,&ok);
            if (!ok)
                return true;
        }
        else
            threshold = std::stoi(param);
        tipl::threshold(dwi,voxel.mask,threshold);
        voxel.steps += cmd + "=" + std::to_string(threshold) + "\n";
        return true;
    }
    if(cmd == "[Step T2a][Remove Background]")
    {
        for(int index = 0;index < voxel.mask.size();++index)
            if(voxel.mask[index] == 0)
                dwi[index] = 0;

        for(int index = 0;index < src_dwi_data.size();++index)
        {
            unsigned short* buf = (unsigned short*)src_dwi_data[index];
            for(int i = 0;i < voxel.mask.size();++i)
                if(voxel.mask[i] == 0)
                    buf[i] = 0;
        }
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Trim]")
    {
        trim();
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Image flip x]")
    {
        flip_dwi(0);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Image flip y]")
    {
        flip_dwi(1);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Image flip z]")
    {
        flip_dwi(2);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Image swap xy]")
    {
        flip_dwi(3);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Image swap yz]")
    {
        flip_dwi(4);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Image swap xz]")
    {
        flip_dwi(5);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Rotate to MNI]")
    {
        begin_prog("rotating");
        rotate_to_mni();
        check_prog(0,0);
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Change b-table:flip bx]")
    {
        for(int i = 0;i < src_bvectors.size();++i)
            src_bvectors[i][0] = -src_bvectors[i][0];
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Change b-table:flip by]")
    {
        for(int i = 0;i < src_bvectors.size();++i)
            src_bvectors[i][1] = -src_bvectors[i][1];
        voxel.steps += cmd+"\n";
        return true;
    }
    if(cmd == "[Step T2][Edit][Change b-table:flip bz]")
    {
        for(int i = 0;i < src_bvectors.size();++i)
            src_bvectors[i][2] = -src_bvectors[i][2];
        voxel.steps += cmd+"\n";
        return true;
    }
    std::cout << "Unknown command:" << cmd << std::endl;
    return false;
}
void ImageModel::flip_b_table(unsigned char dim)
{
    for(unsigned int index = 0;index < src_bvectors.size();++index)
        src_bvectors[index][dim] = -src_bvectors[index][dim];
    if(!voxel.grad_dev.empty())
    {
        // <Flip*Gra_dev*b_table,ODF>
        // = <(Flip*Gra_dev*inv(Flip))*Flip*b_table,ODF>
        unsigned char nindex[3][4] = {{1,2,3,6},{1,3,5,7},{2,5,6,7}};
        for(unsigned int index = 0;index < 4;++index)
        {
            // 1  0  0         1  0  0
            //[0 -1  0] *Grad*[0 -1  0]
            // 0  0  1         0  0  1
            unsigned char pos = nindex[dim][index];
            for(unsigned int i = 0;i < voxel.dim.size();++i)
                voxel.grad_dev[pos][i] = -voxel.grad_dev[pos][i];
        }
    }
}
// 0:xy 1:yz 2: xz
void ImageModel::swap_b_table(unsigned char dim)
{
    std::swap(voxel.vs[dim],voxel.vs[(dim+1)%3]);
    for (unsigned int index = 0;index < src_bvectors.size();++index)
        std::swap(src_bvectors[index][dim],src_bvectors[index][(dim+1)%3]);
    if(!voxel.grad_dev.empty())
    {
        unsigned char swap1[3][6] = {{0,3,6,0,1,2},{1,4,7,3,4,5},{0,3,6,0,1,2}};
        unsigned char swap2[3][6] = {{1,4,7,3,4,5},{2,5,8,6,7,8},{2,5,8,6,7,8}};
        for(unsigned int index = 0;index < 6;++index)
        {
            unsigned char s1 = swap1[dim][index];
            unsigned char s2 = swap2[dim][index];
            for(unsigned int i = 0;i < voxel.dim.size();++i)
                std::swap(voxel.grad_dev[s1][i],voxel.grad_dev[s2][i]);
        }
    }
}

// 0: x  1: y  2: z
// 3: xy 4: yz 5: xz
void ImageModel::flip_dwi(unsigned char type)
{
    if(type < 3)
        flip_b_table(type);
    else
        swap_b_table(type-3);
    tipl::flip(dwi_sum,type);
    tipl::flip(dwi,type);
    tipl::flip(voxel.mask,type);
    for(unsigned int i = 0;i < voxel.grad_dev.size();++i)
    {
        auto I = tipl::make_image((float*)&*(voxel.grad_dev[i].begin()),voxel.dim);
        tipl::flip(I,type);
    }
    for (unsigned int index = 0;check_prog(index,src_dwi_data.size());++index)
    {
        auto I = tipl::make_image((unsigned short*)src_dwi_data[index],voxel.dim);
        tipl::flip(I,type);
    }
    voxel.dim = dwi_sum.geometry();
    voxel.dwi_data.clear();
}
// used in eddy correction for each dwi
void ImageModel::rotate_one_dwi(unsigned int dwi_index,const tipl::transformation_matrix<double>& affine)
{
    tipl::image<float,3> tmp(voxel.dim);
    auto I = tipl::make_image((unsigned short*)src_dwi_data[dwi_index],voxel.dim);
    tipl::resample(I,tmp,affine,tipl::cubic);
    tipl::lower_threshold(tmp,0);
    std::copy(tmp.begin(),tmp.end(),I.begin());
    // rotate b-table
    tipl::matrix<3,3,float> iT = tipl::inverse(affine.get());
    tipl::vector<3> v;
    tipl::vector_rotation(src_bvectors[dwi_index].begin(),v.begin(),iT,tipl::vdim<3>());
    v.normalize();
    src_bvectors[dwi_index] = v;
}

void ImageModel::rotate(const tipl::image<float,3>& ref,
                        const tipl::transformation_matrix<double>& affine,
                        const tipl::image<tipl::vector<3>,3>& cdm_dis,
                        bool super_resolution)
{
    tipl::geometry<3> new_geo = ref.geometry();
    std::vector<tipl::image<unsigned short,3> > dwi(src_dwi_data.size());
    tipl::par_for2(src_dwi_data.size(),[&](unsigned int index,unsigned int id)
    {
        if(!id)
            check_prog(index,src_dwi_data.size());
        dwi[index].resize(new_geo);
        auto I = tipl::make_image((unsigned short*)src_dwi_data[index],voxel.dim);
        if(super_resolution)
            tipl::resample_with_ref(I,ref,dwi[index],affine);
        else
        {
            if(cdm_dis.empty())
                tipl::resample(I,dwi[index],affine,tipl::cubic);
            else
                tipl::resample_dis(I,dwi[index],affine,cdm_dis,tipl::cubic);
        }
        src_dwi_data[index] = &(dwi[index][0]);
    });
    check_prog(0,0);
    tipl::image<unsigned char,3> new_mask(new_geo);
    tipl::resample(voxel.mask,new_mask,affine,tipl::linear);
    voxel.mask.swap(new_mask);
    tipl::morphology::smoothing(voxel.mask);

    dwi.swap(new_dwi);
    // rotate b-table
    if(has_image_rotation)
    {
        tipl::matrix<3,3,float> T = tipl::inverse(affine.get());
        src_bvectors_rotate *= T;
    }
    else
        src_bvectors_rotate = tipl::inverse(affine.get());
    has_image_rotation = true;


    if(!voxel.grad_dev.empty())
    {
        // <R*Gra_dev*b_table,ODF>
        // = <(R*Gra_dev*inv(R))*R*b_table,ODF>
        float det = std::abs(src_bvectors_rotate.det());
        begin_prog("rotating grad_dev");
        for(unsigned int index = 0;check_prog(index,voxel.dim.size());++index)
        {
            tipl::matrix<3,3,float> grad_dev,G_invR;
            for(unsigned int i = 0; i < 9; ++i)
                grad_dev[i] = voxel.grad_dev[i][index];
            G_invR = grad_dev*affine.get();
            grad_dev = src_bvectors_rotate*G_invR;
            for(unsigned int i = 0; i < 9; ++i)
                voxel.grad_dev[i][index] = grad_dev[i]/det;
        }
        std::vector<tipl::image<float,3> > new_gra_dev(voxel.grad_dev.size());
        begin_prog("rotating grad_dev volume");
        for (unsigned int index = 0;check_prog(index,new_gra_dev.size());++index)
        {
            new_gra_dev[index].resize(new_geo);
            tipl::resample(voxel.grad_dev[index],new_gra_dev[index],affine,tipl::cubic);
            voxel.grad_dev[index] = tipl::make_image((float*)&(new_gra_dev[index][0]),voxel.dim);
        }
        new_gra_dev.swap(voxel.new_grad_dev);
    }
    voxel.dim = new_geo;
    voxel.dwi_data.clear();
    calculate_dwi_sum();
}
extern std::string fib_template_file_name_1mm,fib_template_file_name_2mm;
bool ImageModel::rotate_to_mni(void)
{
    std::string file_name;
    if(voxel.vs[0]+voxel.vs[0]+voxel.vs[0] < 6.0)
        file_name = fib_template_file_name_1mm;
    else
        file_name = fib_template_file_name_2mm;

    gz_mat_read read;
    if(!read.load_from_file(file_name.c_str()))
    {
        error_msg = "Failed to load/find fib template.";
        return false;
    }
    tipl::image<float,3> I;
    if(!read.save_to_image(I,"iso"))
    {
        error_msg = "Failed to read image from fib template.";
        return false;
    }
    tipl::vector<3> vs;
    if(!read.get_voxel_size(vs))
    {
        error_msg = "Failed to get voxel size from fib template.";
        return false;
    }

    tipl::transformation_matrix<double> arg;
    bool terminated = false;
    begin_prog("registering to the MNI space");
    check_prog(0,1);
    tipl::reg::two_way_linear_mr(I,vs,dwi_sum,voxel.vs,
                    arg,tipl::reg::rigid_body,tipl::reg::mutual_information(),terminated);
    begin_prog("rotating to the MNI space");
    rotate(I,arg);
    voxel.vs = vs;
    check_prog(1,1);
    return true;
}

void ImageModel::trim(void)
{
    tipl::geometry<3> range_min,range_max;
    tipl::bounding_box(voxel.mask,range_min,range_max,0);
    for (unsigned int index = 0;check_prog(index,src_dwi_data.size());++index)
    {
        auto I = tipl::make_image((unsigned short*)src_dwi_data[index],voxel.dim);
        tipl::image<unsigned short,3> I0 = I;
        tipl::crop(I0,range_min,range_max);
        std::fill(I.begin(),I.end(),0);
        std::copy(I0.begin(),I0.end(),I.begin());
    }
    tipl::crop(voxel.mask,range_min,range_max);
    voxel.dim = voxel.mask.geometry();
    voxel.dwi_data.clear();
    calculate_dwi_sum();
    voxel.calculate_mask(dwi_sum);
}

float interpo_pos(float v1,float v2,float u1,float u2)
{
    float w = (u2-u1-v2+v1);
    return std::max<float>(0.0,std::min<float>(1.0,w == 0.0f? 0:(v1-u1)/w));
}

template<typename image_type>
void get_distortion_map(const image_type& v1,
                        const image_type& v2,
                        tipl::image<float,3>& dis_map)
{
    int h = v1.height(),w = v1.width();
    dis_map.resize(v1.geometry());
    tipl::par_for(v1.depth()*h,[&](int z)
    {
        int base_pos = z*w;
        std::vector<float> cdf_x1(w),cdf_x2(w);//,cdf(h);
        tipl::pdf2cdf(v1.begin()+base_pos,v1.begin()+base_pos+w,&cdf_x1[0]);
        tipl::pdf2cdf(v2.begin()+base_pos,v2.begin()+base_pos+w,&cdf_x2[0]);
        if(cdf_x1.back() == 0.0 || cdf_x2.back() == 0.0)
            return;

        tipl::multiply_constant(cdf_x2,(cdf_x1.back()+cdf_x2.back())*0.5f/cdf_x2.back());
        tipl::add_constant(cdf_x2,(cdf_x1.back()-cdf_x2.back())*0.5f);

        for(int x = 0,pos = base_pos;x < w;++x,++pos)
        {
            if(cdf_x1[x] == cdf_x2[x])
            {
                //cdf[y] = cdf_y1[y];
                continue;
            }
            int d = 1,x1,x2;
            float v1,v2,u1,u2;
            v2 = cdf_x1[x];
            u2 = cdf_x2[x];
            bool positive_d = true;
            if(cdf_x1[x] > cdf_x2[x])
            {
                for(;d < w;++d)
                {
                    x1 = x-d;
                    x2 = x+d;
                    v1 = v2;
                    u1 = u2;
                    v2 = (x1 >=0 ? cdf_x1[x1]:0);
                    u2 = (x2 < cdf_x2.size() ? cdf_x2[x2]:cdf_x2.back());
                    if(v2 <= u2)
                        break;
                }
            }
            else
            {
                for(;d < h;++d)
                {
                    x2 = x-d;
                    x1 = x+d;
                    v1 = v2;
                    u1 = u2;
                    v2 = (x1 < cdf_x1.size() ? cdf_x1[x1]:cdf_x1.back());
                    u2 = (x2 >= 0 ? cdf_x2[x2]:0);
                    if(v2 >= u2)
                        break;
                }
                positive_d = false;
            }
            //cdf[y] = v1+(v2-v1)*interpo_pos(v1,v2,u1,u2);
            dis_map[pos] = interpo_pos(v1,v2,u1,u2)+d-1;
            if(!positive_d)
                dis_map[pos] = -dis_map[pos];
        }
    }
    );
}

template<typename image_type>
void apply_distortion_map2(const image_type& v1,
                          const tipl::image<float,3>& dis_map,
                          image_type& v1_out,bool positive)
{
    int w = v1.width();
    v1_out.resize(v1.geometry());
    tipl::par_for(v1.depth()*v1.height(),[&](int z)
    {
        std::vector<float> cdf_x1(w),cdf(w);
        int base_pos = z*w;
        {
            tipl::pdf2cdf(v1.begin()+base_pos,v1.begin()+base_pos+w,&cdf_x1[0]);
            auto I1 = tipl::make_image(&cdf_x1[0],tipl::geometry<1>(w));
            for(int x = 0,pos = base_pos;x < w;++x,++pos)
                cdf[x] = tipl::estimate(I1,positive ? x+dis_map[pos] : x-dis_map[pos]);
            for(int x = 0,pos = base_pos;x < w;++x,++pos)
                v1_out[pos] = (x? std::max<float>(0.0f,cdf[x] - cdf[x-1]):0);
        }
    }
    );
}




void ImageModel::distortion_correction(const ImageModel& rhs)
{
    tipl::image<float,3> v1,v2,vv1,vv2;
    //v1 = dwi_sum;
    //v2 = rhs.dwi_sum;
    v1 = tipl::make_image(src_dwi_data[0],voxel.dim);
    v2 = tipl::make_image(rhs.src_dwi_data[0],voxel.dim);


    bool swap_xy = false;
    {
        tipl::image<float,2> px1,px2,py1,py2;
        tipl::project_x(v1,px1);
        tipl::project_x(v2,px2);
        tipl::project_y(v1,py1);
        tipl::project_y(v2,py2);
        float cx = tipl::correlation(px1.begin(),px1.end(),px2.begin());
        float cy = tipl::correlation(py1.begin(),py1.end(),py2.begin());

        if(cx < cy)
        {
            tipl::swap_xy(v1);
            tipl::swap_xy(v2);
            swap_xy = true;
        }
    }

    tipl::image<float,3> dis_map(v1.geometry()),df,gx(v1.geometry()),v1_gx(v1.geometry()),v2_gx(v2.geometry());


    tipl::filter::gaussian(v1);
    tipl::filter::gaussian(v2);

    tipl::gradient(v1,v1_gx,1,0);
    tipl::gradient(v2,v2_gx,1,0);

    get_distortion_map(v2,v1,dis_map);
    tipl::filter::gaussian(dis_map);
    tipl::filter::gaussian(dis_map);


    for(int iter = 0;iter < 120;++iter)
    {
        apply_distortion_map2(v1,dis_map,vv1,true);
        apply_distortion_map2(v2,dis_map,vv2,false);
        df = vv1;
        df -= vv2;
        vv1 += vv2;
        df *= vv1;
        tipl::gradient(df,gx,1,0);
        gx += v1_gx;
        gx -= v2_gx;
        tipl::normalize_abs(gx,0.5f);
        tipl::filter::gaussian(gx);
        tipl::filter::gaussian(gx);
        tipl::filter::gaussian(gx);
        dis_map += gx;
    }


    //dwi_sum = dis_map;
    //return;


    std::vector<tipl::image<unsigned short,3> > dwi(src_dwi_data.size());
    for(int i = 0;i < src_dwi_data.size();++i)
    {
        v1 = tipl::make_image(src_dwi_data[i],voxel.dim);
        v2 = tipl::make_image(rhs.src_dwi_data[i],rhs.voxel.dim);
        if(swap_xy)
        {
            tipl::swap_xy(v1);
            tipl::swap_xy(v2);
        }
        apply_distortion_map2(v1,dis_map,vv1,true);
        apply_distortion_map2(v2,dis_map,vv2,false);
        dwi[i] = vv1;
        if(swap_xy)
            tipl::swap_xy(dwi[i]);
    }


    new_dwi.swap(dwi);
    for(int i = 0;i < new_dwi.size();++i)
        src_dwi_data[i] = &(new_dwi[i][0]);

    calculate_dwi_sum();
    voxel.calculate_mask(dwi_sum);

}

void calculate_shell(const std::vector<float>& sorted_bvalues,
                     std::vector<unsigned int>& shell)
{
    if(sorted_bvalues.front() != 0.0f)
        shell.push_back(0);
    else
    {
        for(int i = 1;i < sorted_bvalues.size();++i)
            if(sorted_bvalues[i] != 0)
            {
                shell.push_back(i);
                break;
            }
    }
    for(unsigned int index = shell.back()+1;index < sorted_bvalues.size();++index)
        if(std::abs(sorted_bvalues[index]-sorted_bvalues[index-1]) > 100)
            shell.push_back(index);
}

void ImageModel::calculate_shell(void)
{
    std::vector<float> sorted_bvalues(src_bvalues);
    std::sort(sorted_bvalues.begin(),sorted_bvalues.end());
    ::calculate_shell(sorted_bvalues,shell);
}
bool ImageModel::is_dsi_half_sphere(void)
{
    if(shell.empty())
        calculate_shell();
    return is_dsi() && (shell[1] - shell[0] <= 3);
}

bool ImageModel::is_dsi(void)
{
    if(shell.empty())
        calculate_shell();
    return shell.size() > 4 && (shell[1] - shell[0] <= 6);
}
bool ImageModel::need_scheme_balance(void)
{
    if(shell.empty())
        calculate_shell();
    if(is_dsi() || shell.size() > 6)
        return false;
    for(int i = 0;i < shell.size();++i)
    {
        unsigned int from = shell[i];
        unsigned int to = (i + 1 == shell.size() ? src_bvalues.size():shell[i+1]);
        if(to-from < 128)
            return true;
    }
    return false;
}

bool ImageModel::is_multishell(void)
{
    if(shell.empty())
        calculate_shell();
    return (shell.size() > 1) && !is_dsi();
}


void ImageModel::get_report(std::string& report)
{
    std::vector<float> sorted_bvalues(src_bvalues);
    std::sort(sorted_bvalues.begin(),sorted_bvalues.end());
    unsigned int num_dir = 0;
    for(int i = 0;i < src_bvalues.size();++i)
        if(src_bvalues[i] > 50)
            ++num_dir;
    std::ostringstream out;
    if(is_dsi())
    {
        out << " A diffusion spectrum imaging scheme was used, and a total of " << num_dir
            << " diffusion sampling were acquired."
            << " The maximum b-value was " << (int)std::round(src_bvalues.back()) << " s/mm2.";
    }
    else
    if(is_multishell())
    {
        out << " A multishell diffusion scheme was used, and the b-values were ";
        for(unsigned int index = 0;index < shell.size();++index)
        {
            if(index > 0)
            {
                if(index == shell.size()-1)
                    out << " and ";
                else
                    out << " ,";
            }
            out << (int)std::round(sorted_bvalues[
                index == shell.size()-1 ? (sorted_bvalues.size()+shell.back())/2 : (shell[index+1] + shell[index])/2]);
        }
        out << " s/mm2.";

        out << " The number of diffusion sampling directions were ";
        for(unsigned int index = 0;index < shell.size()-1;++index)
            out << shell[index+1] - shell[index] << (shell.size() == 2 ? " ":", ");
        out << "and " << sorted_bvalues.size()-shell.back() << ", respectively.";
    }
    else
        if(shell.size() == 1)
        {
            if(num_dir < 100)
                out << " A DTI diffusion scheme was used, and a total of ";
            else
                out << " A HARDI scheme was used, and a total of ";
            out << num_dir
                << " diffusion sampling directions were acquired."
                << " The b-value was " << sorted_bvalues.back() << " s/mm2.";
        }

    out << " The in-plane resolution was " << voxel.vs[0] << " mm."
        << " The slice thickness was " << voxel.vs[2] << " mm.";
    report = out.str();
}

bool ImageModel::load_from_file(const char* dwi_file_name)
{
    file_name = dwi_file_name;
    if (!mat_reader.load_from_file(dwi_file_name))
    {
        error_msg = "Cannot open file";
        return false;
    }
    unsigned int row,col;

    const unsigned short* dim_ptr = 0;
    if (!mat_reader.read("dimension",row,col,dim_ptr))
    {
        error_msg = "Cannot find dimension matrix";
        return false;
    }
    const float* voxel_size = 0;
    if (!mat_reader.read("voxel_size",row,col,voxel_size))
    {
        //error_msg = "Cannot find voxel size matrix";
        //return false;
        std::fill(voxel.vs.begin(),voxel.vs.end(),2.0);
    }
    else
        std::copy(voxel_size,voxel_size+3,voxel.vs.begin());

    if (dim_ptr[0]*dim_ptr[1]*dim_ptr[2] <= 0)
    {
        error_msg = "Invalid dimension setting";
        return false;
    }
    voxel.dim[0] = dim_ptr[0];
    voxel.dim[1] = dim_ptr[1];
    voxel.dim[2] = dim_ptr[2];

    const float* table;
    if (!mat_reader.read("b_table",row,col,table))
    {
        error_msg = "Cannot find b_table matrix";
        return false;
    }
    src_bvalues.resize(col);
    src_bvectors.resize(col);
    for (unsigned int index = 0;index < col;++index)
    {
        src_bvalues[index] = table[0];
        src_bvectors[index][0] = table[1];
        src_bvectors[index][1] = table[2];
        src_bvectors[index][2] = table[3];
        src_bvectors[index].normalize();
        table += 4;
    }

    const char* report_buf = 0;
    if(mat_reader.read("report",row,col,report_buf))
        voxel.report = std::string(report_buf,report_buf+row*col);
    else
        get_report(voxel.report);

    src_dwi_data.resize(src_bvalues.size());
    for (unsigned int index = 0;index < src_bvalues.size();++index)
    {
        std::ostringstream out;
        out << "image" << index;
        mat_reader.read(out.str().c_str(),row,col,src_dwi_data[index]);
        if (!src_dwi_data[index])
        {
            error_msg = "Cannot find image matrix";
            return false;
        }
    }


    {
        const float* grad_dev = 0;
        if(mat_reader.read("grad_dev",row,col,grad_dev) && row*col == voxel.dim.size()*9)
        {
            for(unsigned int index = 0;index < 9;index++)
                voxel.grad_dev.push_back(tipl::make_image((float*)grad_dev+index*voxel.dim.size(),voxel.dim));
            if(std::fabs(voxel.grad_dev[0][0])+std::fabs(voxel.grad_dev[4][0])+std::fabs(voxel.grad_dev[8][0]) < 1.0)
            {
                tipl::add_constant(voxel.grad_dev[0].begin(),voxel.grad_dev[0].end(),1.0);
                tipl::add_constant(voxel.grad_dev[4].begin(),voxel.grad_dev[4].end(),1.0);
                tipl::add_constant(voxel.grad_dev[8].begin(),voxel.grad_dev[8].end(),1.0);
            }
        }

    }

    // create mask;
    calculate_dwi_sum();

    const unsigned char* mask_ptr = 0;
    if(mat_reader.read("mask",row,col,mask_ptr))
    {
        voxel.mask.resize(voxel.dim);
        if(row*col == voxel.dim.size())
            std::copy(mask_ptr,mask_ptr+row*col,voxel.mask.begin());
    }
    else
        voxel.calculate_mask(dwi_sum);
    voxel.steps += "[Step T2][Reconstruction] open ";
    voxel.steps += QFileInfo(dwi_file_name).fileName().toStdString();
    voxel.steps += "\n";
    return true;
}

void ImageModel::save_to_file(gz_mat_write& mat_writer)
{

    set_title("Saving");

    // dimension
    {
        short dim[3];
        dim[0] = voxel.dim[0];
        dim[1] = voxel.dim[1];
        dim[2] = voxel.dim[2];
        mat_writer.write("dimension",dim,1,3);
    }

    // voxel size
    mat_writer.write("voxel_size",&*voxel.vs.begin(),1,3);

    std::vector<float> float_data;
    std::vector<short> short_data;
    voxel.ti.save_to_buffer(float_data,short_data);
    mat_writer.write("odf_vertices",&*float_data.begin(),3,voxel.ti.vertices_count);
    mat_writer.write("odf_faces",&*short_data.begin(),3,voxel.ti.faces.size());

}
void ImageModel::save_fib(const std::string& ext)
{
    std::string output_name = file_name;
    output_name += ext;
    gz_mat_write mat_writer(output_name.c_str());
    save_to_file(mat_writer);
    voxel.end(mat_writer);
    std::string final_report = voxel.report;
    final_report += voxel.recon_report.str();
    mat_writer.write("report",final_report.c_str(),1,final_report.length());
    std::string final_steps = voxel.steps;
    final_steps += voxel.step_report.str();
    final_steps += "[Step T2b][Run reconstruction]\n";
    mat_writer.write("steps",final_steps.c_str(),1,final_steps.length());
}
bool ImageModel::save_to_nii(const char* nifti_file_name) const
{
    gz_nifti header;
    header.set_voxel_size(voxel.vs);
    header.nif_header.pixdim[0] = 4;
    header.nif_header2.pixdim[0] = 4;

    tipl::geometry<4> nifti_dim;
    std::copy(voxel.dim.begin(),voxel.dim.end(),nifti_dim.begin());
    nifti_dim[3] = src_bvalues.size();
    tipl::image<unsigned short,4> buffer(nifti_dim);
    for(unsigned int index = 0;index < src_bvalues.size();++index)
    {
        std::copy(src_dwi_data[index],
                  src_dwi_data[index]+voxel.dim.size(),
                  buffer.begin() + (size_t)index*voxel.dim.size());
    }
    tipl::flip_xy(buffer);
    header << buffer;
    return header.save_to_file(nifti_file_name);
}
bool ImageModel::save_b0_to_nii(const char* nifti_file_name) const
{
    gz_nifti header;
    header.set_voxel_size(voxel.vs);
    tipl::image<unsigned short,3> buffer(src_dwi_data[0],voxel.dim);
    tipl::flip_xy(buffer);
    header << buffer;
    return header.save_to_file(nifti_file_name);
}

bool ImageModel::save_dwi_sum_to_nii(const char* nifti_file_name) const
{
    gz_nifti header;
    header.set_voxel_size(voxel.vs);
    tipl::image<float,3> buffer(dwi_sum);
    tipl::flip_xy(buffer);
    header << buffer;
    return header.save_to_file(nifti_file_name);
}

bool ImageModel::save_b_table(const char* file_name) const
{
    std::ofstream out(file_name);
    for(unsigned int index = 0;index < src_bvalues.size();++index)
    {
        out << src_bvalues[index] << " "
            << src_bvectors[index][0] << " "
            << src_bvectors[index][1] << " "
            << src_bvectors[index][2] << std::endl;
    }
    return out.good();
}
bool ImageModel::save_bval(const char* file_name) const
{
    std::ofstream out(file_name);
    for(unsigned int index = 0;index < src_bvalues.size();++index)
    {
        if(index)
            out << " ";
        out << src_bvalues[index];
    }
    return out.good();
}
bool ImageModel::save_bvec(const char* file_name) const
{
    std::ofstream out(file_name);
    for(unsigned int index = 0;index < src_bvalues.size();++index)
    {
        out << src_bvectors[index][0] << " "
            << src_bvectors[index][1] << " "
            << src_bvectors[index][2] << std::endl;
    }
    return out.good();
}
bool ImageModel::compare_src(const char* file_name)
{
    std::shared_ptr<ImageModel> bl(new ImageModel);
    begin_prog("reading");
    if(!bl->load_from_file(file_name))
    {
        error_msg = bl->error_msg;
        return false;
    }
    study_src = bl;

    voxel.study_name = QFileInfo(file_name).baseName().toStdString();
    voxel.compare_voxel = &(study_src->voxel);

    begin_prog("Registration between longitudinal scans");
    {
        tipl::transformation_matrix<double> arg;
        bool terminated = false;
        check_prog(0,1);
        tipl::reg::two_way_linear_mr(dwi_sum,voxel.vs,
                                     study_src->dwi_sum,study_src->voxel.vs,
                                        arg,tipl::reg::rigid_body,tipl::reg::correlation(),terminated);
        // nonlinear part
        tipl::image<tipl::vector<3>,3> cdm_dis;
        if(voxel.dt_deform)
        {
            tipl::image<float,3> new_dwi_sum(dwi_sum.geometry());
            tipl::resample(study_src->dwi_sum,new_dwi_sum,arg,tipl::cubic);
            tipl::match_signal(dwi_sum,new_dwi_sum);
            bool terminated = false;
            begin_prog("Nonlinear registration between longitudinal scans");
            tipl::reg::cdm(dwi_sum,new_dwi_sum,cdm_dis,terminated);
            check_prog(0,1);

            /*
            if(1) // debug
            {
                tipl::image<float,3> result(dwi_sum.geometry());
                tipl::resample_dis(study_src->dwi_sum,result,arg,cdm_dis,tipl::cubic);
                gz_nifti o1,o2,o3;
                o1.set_voxel_size(voxel.vs);
                o1.load_from_image(dwi_sum);
                o1.save_to_file("d:/1.nii.gz");

                o2.set_voxel_size(study_src->voxel.vs);
                o2.load_from_image(new_dwi_sum);
                o2.save_to_file("d:/2.nii.gz");

                o3.set_voxel_size(study_src->voxel.vs);
                o3.load_from_image(result);
                o3.save_to_file("d:/3.nii.gz");
            }*/
        }
        study_src->rotate(dwi_sum,arg,cdm_dis);
        study_src->voxel.vs = voxel.vs;
        study_src->voxel.mask = voxel.mask;
        check_prog(1,1);
    }


    // correct b_table first
    if(voxel.check_btable)
        study_src->check_b_table();


    for(int i = 0;i < voxel.mask.size();++i)
        if(study_src->src_dwi_data[0][i] == 0)
            voxel.mask[i] = 0;



    // Signal match on b0 to allow for quantitative MRI in DDI
    {
        std::vector<double> r;
        for(int i = 0;i < voxel.mask.size();++i)
            if(voxel.mask[i])
            {
                if(study_src->src_dwi_data[0][i] && src_dwi_data[0][i])
                    r.push_back((float)src_dwi_data[0][i]/(float)study_src->src_dwi_data[0][i]);
            }

        double median_r = tipl::median(r.begin(),r.end());
        std::cout << "median_r=" << median_r << std::endl;
        tipl::par_for(study_src->new_dwi.size(),[&](int i)
        {
            tipl::multiply_constant(study_src->new_dwi[i].begin(),study_src->new_dwi[i].end(),median_r);
        });
        study_src->calculate_dwi_sum();
    }
    pre_dti();
    study_src->pre_dti();
    voxel.R2 = tipl::correlation(dwi_sum.begin(),dwi_sum.end(),study_src->dwi_sum.begin());
    return true;
}
