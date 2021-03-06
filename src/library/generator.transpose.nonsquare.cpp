/* ************************************************************************
* Copyright 2013 Advanced Micro Devices, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
* ************************************************************************/


// clfft.generator.Transpose.cpp : Dynamic run-time generator of openCL transpose kernels
//

// TODO: generalize the kernel to work with any size

#include "stdafx.h"

#include <math.h>
#include <iomanip>

#include "generator.transpose.nonsquare.h"
#include "generator.stockham.h"

#include "action.h"

FFTGeneratedTransposeNonSquareAction::FFTGeneratedTransposeNonSquareAction(clfftPlanHandle plHandle, FFTPlan * plan, cl_command_queue queue, clfftStatus & err)
    : FFTTransposeNonSquareAction(plHandle, plan, queue, err)
{
    if (err != CLFFT_SUCCESS)
    {
        // FFTTransposeNonSquareAction() failed, exit
        fprintf(stderr, "FFTTransposeNonSquareAction() failed!\n");
        return;
    }

    // Initialize the FFTAction::FFTKernelGenKeyParams member
    err = this->initParams();

    if (err != CLFFT_SUCCESS)
    {
        fprintf(stderr, "FFTGeneratedTransposeNonSquareAction::initParams() failed!\n");
        return;
    }

    FFTRepo &fftRepo = FFTRepo::getInstance();

    err = this->generateKernel(fftRepo, queue);

    if (err != CLFFT_SUCCESS)
    {
        fprintf(stderr, "FFTGeneratedTransposeNonSquareAction::generateKernel failed\n");
        return;
    }

    err = compileKernels(queue, plHandle, plan);

    if (err != CLFFT_SUCCESS)
    {
        fprintf(stderr, "FFTGeneratedTransposeNonSquareAction::compileKernels failed\n");
        return;
    }

    err = CLFFT_SUCCESS;
}


bool FFTGeneratedTransposeNonSquareAction::buildForwardKernel()
{
    clfftLayout inputLayout = this->getSignatureData()->fft_inputLayout;
    clfftLayout outputLayout = this->getSignatureData()->fft_outputLayout;

    bool r2c_transform = (inputLayout == CLFFT_REAL);
    bool c2r_transform = (outputLayout == CLFFT_REAL);
    bool real_transform = (r2c_transform || c2r_transform);

    return (!real_transform) || r2c_transform;
}

bool FFTGeneratedTransposeNonSquareAction::buildBackwardKernel()
{
    clfftLayout inputLayout = this->getSignatureData()->fft_inputLayout;
    clfftLayout outputLayout = this->getSignatureData()->fft_outputLayout;

    bool r2c_transform = (inputLayout == CLFFT_REAL);
    bool c2r_transform = (outputLayout == CLFFT_REAL);
    bool real_transform = (r2c_transform || c2r_transform);

    return (!real_transform) || c2r_transform;
}



inline std::stringstream& clKernWrite(std::stringstream& rhs, const size_t tabIndex)
{
    rhs << std::setw(tabIndex) << "";
    return rhs;
}



static void OffsetCalc(std::stringstream& transKernel, const FFTKernelGenKeyParams& params)
{
    const size_t *stride =  params.fft_inStride;
    std::string offset =  "iOffset";

    clKernWrite(transKernel, 3) << "size_t " << offset << " = 0;" << std::endl;
    clKernWrite(transKernel, 3) << "g_index = get_group_id(0);" << std::endl;

    for (size_t i = params.fft_DataDim - 2; i > 0; i--)
    {
        clKernWrite(transKernel, 3) << offset << " += (g_index/numGroupsY_" << i << ")*" << stride[i + 1] << ";" << std::endl;
        clKernWrite(transKernel, 3) << "g_index = g_index % numGroupsY_" << i << ";" << std::endl;
    }
    
    clKernWrite(transKernel, 3) << std::endl;
}


static void Swap_OffsetCalc(std::stringstream& transKernel, const FFTKernelGenKeyParams& params)
{
    const size_t *stride = params.fft_inStride;
    std::string offset = "iOffset";

    clKernWrite(transKernel, 3) << "size_t " << offset << " = 0;" << std::endl;

    for (size_t i = params.fft_DataDim - 2; i > 0; i--)
    {
        clKernWrite(transKernel, 3) << offset << " += (g_index/numGroupsY_" << i << ")*" << stride[i + 1] << ";" << std::endl;
        clKernWrite(transKernel, 3) << "g_index = g_index % numGroupsY_" << i << ";" << std::endl;
    }

    clKernWrite(transKernel, 3) << std::endl;
}

// Small snippet of code that multiplies the twiddle factors into the butterfiles.  It is only emitted if the plan tells
// the generator that it wants the twiddle factors generated inside of the transpose
static clfftStatus genTwiddleMath(const FFTKernelGenKeyParams& params, std::stringstream& transKernel, const std::string& dtComplex, bool fwd)
{

    clKernWrite(transKernel, 9) << std::endl;
    if (params.fft_N[0] > params.fft_N[1])
    {
        clKernWrite(transKernel, 9) << dtComplex << " Wm = TW3step( ("<< params.fft_N[1] <<" * square_matrix_index + t_gx_p*32 + lidx) * (t_gy_p*32 + lidy + loop*8) );" << std::endl;
        clKernWrite(transKernel, 9) << dtComplex << " Wt = TW3step( ("<< params.fft_N[1] <<" * square_matrix_index + t_gy_p*32 + lidx) * (t_gx_p*32 + lidy + loop*8) );" << std::endl;
    }
    else
    {
        clKernWrite(transKernel, 9) << dtComplex << " Wm = TW3step( (t_gx_p*32 + lidx) * (" << params.fft_N[0] << " * square_matrix_index + t_gy_p*32 + lidy + loop*8) );" << std::endl;
        clKernWrite(transKernel, 9) << dtComplex << " Wt = TW3step( (t_gy_p*32 + lidx) * (" << params.fft_N[0] << " * square_matrix_index + t_gx_p*32 + lidy + loop*8) );" << std::endl;
    }
    clKernWrite(transKernel, 9) << dtComplex << " Tm, Tt;" << std::endl;

    if (fwd)
    {
        clKernWrite(transKernel, 9) << "Tm.x = ( Wm.x * tmpm.x ) - ( Wm.y * tmpm.y );" << std::endl;
        clKernWrite(transKernel, 9) << "Tm.y = ( Wm.y * tmpm.x ) + ( Wm.x * tmpm.y );" << std::endl;
        clKernWrite(transKernel, 9) << "Tt.x = ( Wt.x * tmpt.x ) - ( Wt.y * tmpt.y );" << std::endl;
        clKernWrite(transKernel, 9) << "Tt.y = ( Wt.y * tmpt.x ) + ( Wt.x * tmpt.y );" << std::endl;
    }
    else
    {
        clKernWrite(transKernel, 9) << "Tm.x =  ( Wm.x * tmpm.x ) + ( Wm.y * tmpm.y );" << std::endl;
        clKernWrite(transKernel, 9) << "Tm.y = -( Wm.y * tmpm.x ) + ( Wm.x * tmpm.y );" << std::endl;
        clKernWrite(transKernel, 9) << "Tt.x =  ( Wt.x * tmpt.x ) + ( Wt.y * tmpt.y );" << std::endl;
        clKernWrite(transKernel, 9) << "Tt.y = -( Wt.y * tmpt.x ) + ( Wt.x * tmpt.y );" << std::endl;
    }

    clKernWrite(transKernel, 9) << "tmpm.x = Tm.x;" << std::endl;
    clKernWrite(transKernel, 9) << "tmpm.y = Tm.y;" << std::endl;
    clKernWrite(transKernel, 9) << "tmpt.x = Tt.x;" << std::endl;
    clKernWrite(transKernel, 9) << "tmpt.y = Tt.y;" << std::endl;

    clKernWrite(transKernel, 9) << std::endl;

    return CLFFT_SUCCESS;
}

// These strings represent the names that are used as strKernel parameters
const std::string pmRealIn("pmRealIn");
const std::string pmImagIn("pmImagIn");
const std::string pmRealOut("pmRealOut");
const std::string pmImagOut("pmImagOut");
const std::string pmComplexIn("pmComplexIn");
const std::string pmComplexOut("pmComplexOut");

static clfftStatus genTransposePrototype(const FFTGeneratedTransposeNonSquareAction::Signature & params, const size_t& lwSize, const std::string& dtPlanar, const std::string& dtComplex,
    const std::string &funcName, std::stringstream& transKernel, std::string& dtInput, std::string& dtOutput)
{

    // Declare and define the function
    clKernWrite(transKernel, 0) << "__attribute__(( reqd_work_group_size( " << lwSize << ", 1, 1 ) ))" << std::endl;
    clKernWrite(transKernel, 0) << "kernel void" << std::endl;

    clKernWrite(transKernel, 0) << funcName << "( ";

    switch (params.fft_inputLayout)
    {
    case CLFFT_COMPLEX_INTERLEAVED:
        dtInput = dtComplex;
        dtOutput = dtComplex;
        clKernWrite(transKernel, 0) << "global " << dtInput << "* restrict inputA";
        break;
    case CLFFT_COMPLEX_PLANAR:
        dtInput = dtPlanar;
        dtOutput = dtPlanar;
        clKernWrite(transKernel, 0) << "global " << dtInput << "* restrict inputA_R" << ", global " << dtInput << "* restrict inputA_I";
        break;
    case CLFFT_HERMITIAN_INTERLEAVED:
    case CLFFT_HERMITIAN_PLANAR:
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    case CLFFT_REAL:
        dtInput = dtPlanar;
        dtOutput = dtPlanar;

        clKernWrite(transKernel, 0) << "global " << dtInput << "* restrict inputA";
        break;
    default:
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    }

    if (params.fft_hasPreCallback)
    {
		assert(!params.fft_hasPostCallback);
        if (params.fft_preCallback.localMemSize > 0)
        {
            clKernWrite(transKernel, 0) << ", __global void* pre_userdata, __local void* localmem";
        }
        else
        {
            clKernWrite(transKernel, 0) << ", __global void* pre_userdata";
        }
    }
	if (params.fft_hasPostCallback)
	{
		assert(!params.fft_hasPreCallback);

		if (params.fft_postCallback.localMemSize > 0)
		{
			clKernWrite( transKernel, 0 ) << ", __global void* post_userdata, __local void* localmem";
		}
		else
		{
			clKernWrite( transKernel, 0 ) << ", __global void* post_userdata";
        }
    }


    // Close the method signature
    clKernWrite(transKernel, 0) << " )\n{" << std::endl;
    return CLFFT_SUCCESS;
}

/* -> get_cycles function gets the swapping logic required for given row x col matrix.
-> cycle_map[0] holds the total number of cycles required.
-> cycles start and end with the same index, hence we can identify individual cycles,
though we tend to store the cycle index contiguously*/
static void get_cycles(size_t *cycle_map, size_t num_reduced_row, size_t num_reduced_col)
{
    int *is_swapped = new int[num_reduced_row * num_reduced_col];
    int i, map_index = 1, num_cycles = 0;
    size_t swap_id;
    /*initialize swap map*/
    is_swapped[0] = 1;
    is_swapped[num_reduced_row * num_reduced_col - 1] = 1;
    for (i = 1; i < (num_reduced_row * num_reduced_col - 1); i++)
    {
        is_swapped[i] = 0;
    }

    for (i = 1; i < (num_reduced_row * num_reduced_col - 1); i++)
    {
        swap_id = i;
        while (!is_swapped[swap_id])
        {
            is_swapped[swap_id] = 1;
            cycle_map[map_index++] = swap_id;
            swap_id = (num_reduced_row * swap_id) % (num_reduced_row * num_reduced_col - 1);
            if (swap_id == i)
            {
                cycle_map[map_index++] = swap_id;
                num_cycles++;
            }
        }
    }
    cycle_map[0] = num_cycles;
    delete[] is_swapped;
}

static clfftStatus genSwapKernel(const FFTGeneratedTransposeNonSquareAction::Signature & params, std::string& strKernel, const size_t& lwSize, const size_t reShapeFactor)
{
    strKernel.reserve(4096);
    std::stringstream transKernel(std::stringstream::out);

    // These strings represent the various data types we read or write in the kernel, depending on how the plan
    // is configured
    std::string dtInput;        // The type read as input into kernel
    std::string dtOutput;       // The type written as output from kernel
    std::string dtPlanar;       // Fundamental type for planar arrays
    std::string tmpBuffType;
    std::string dtComplex;      // Fundamental type for complex arrays

                                // NOTE:  Enable only for debug
                                // clKernWrite( transKernel, 0 ) << "#pragma OPENCL EXTENSION cl_amd_printf : enable\n" << std::endl;

                                //if (params.fft_inputLayout != params.fft_outputLayout)
                                //	return CLFFT_TRANSPOSED_NOTIMPLEMENTED;

    switch (params.fft_precision)
    {
    case CLFFT_SINGLE:
    case CLFFT_SINGLE_FAST:
        dtPlanar = "float";
        dtComplex = "float2";
        break;
    case CLFFT_DOUBLE:
    case CLFFT_DOUBLE_FAST:
        dtPlanar = "double";
        dtComplex = "double2";

        // Emit code that enables double precision in the kernel
        clKernWrite(transKernel, 0) << "#ifdef cl_khr_fp64" << std::endl;
        clKernWrite(transKernel, 3) << "#pragma OPENCL EXTENSION cl_khr_fp64 : enable" << std::endl;
        clKernWrite(transKernel, 0) << "#else" << std::endl;
        clKernWrite(transKernel, 3) << "#pragma OPENCL EXTENSION cl_amd_fp64 : enable" << std::endl;
        clKernWrite(transKernel, 0) << "#endif\n" << std::endl;

        break;
    default:
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        break;
    }

    // This detects whether the input matrix is rectangle of ratio 1:2

    if ((params.fft_N[0] != 2 * params.fft_N[1]) && (params.fft_N[1] != 2 * params.fft_N[0]))
    {
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    }

    if (params.fft_placeness == CLFFT_OUTOFPLACE)
    {
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    }

    size_t smaller_dim = (params.fft_N[0] < params.fft_N[1]) ? params.fft_N[0] : params.fft_N[1];

    size_t input_elm_size_in_bytes;
    switch (params.fft_precision)
    {
    case CLFFT_SINGLE:
    case CLFFT_SINGLE_FAST:
        input_elm_size_in_bytes = 4;
        break;
    case CLFFT_DOUBLE:
    case CLFFT_DOUBLE_FAST:
        input_elm_size_in_bytes = 8;
        break;
    default:
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    }

    switch (params.fft_outputLayout)
    {
    case CLFFT_COMPLEX_INTERLEAVED:
    case CLFFT_COMPLEX_PLANAR:
        input_elm_size_in_bytes *= 2;
        break;
    case CLFFT_REAL:
        break;
    default:
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    }
    size_t max_elements_loaded = AVAIL_MEM_SIZE / input_elm_size_in_bytes;
    size_t num_elements_loaded;
    size_t local_work_size_swap, num_grps_pro_row;

    tmpBuffType = "__local";
    if ((max_elements_loaded >> 1) > smaller_dim)
    {
        local_work_size_swap = (smaller_dim < 256) ? smaller_dim : 256;
        num_elements_loaded = smaller_dim;
        num_grps_pro_row = 1;
    }
    else
    {
        num_grps_pro_row = (smaller_dim << 1) / max_elements_loaded;
        num_elements_loaded = max_elements_loaded >> 1;
        local_work_size_swap = (num_elements_loaded < 256) ? num_elements_loaded : 256;
    }
    
	//If post-callback is set for the plan
	if (params.fft_hasPostCallback)
	{
		//Requested local memory size by callback must not exceed the device LDS limits after factoring the LDS size required by swap kernel
		if (params.fft_postCallback.localMemSize > 0)
		{
			bool validLDSSize = false;
			
			validLDSSize = ((2 * input_elm_size_in_bytes * (num_elements_loaded * 2)) + params.fft_postCallback.localMemSize) < params.limit_LocalMemSize;
		
			if(!validLDSSize)
			{
				fprintf(stderr, "Requested local memory size not available\n");
				return CLFFT_INVALID_ARG_VALUE;
			}
		}

		//Insert callback function code at the beginning 
		clKernWrite( transKernel, 0 ) << params.fft_postCallback.funcstring << std::endl;
		clKernWrite( transKernel, 0 ) << std::endl;
	}

    /*Generating the  swapping logic*/
    {
        size_t num_reduced_row;
        size_t num_reduced_col;

        if (params.fft_N[1] == smaller_dim)
        {
            num_reduced_row = smaller_dim;
            num_reduced_col = 2;
        }
        else
        {
            num_reduced_row = 2;
            num_reduced_col = smaller_dim;
        }

        std::string funcName;

        clKernWrite(transKernel, 0) << std::endl;

        size_t *cycle_map = new size_t[num_reduced_row * num_reduced_col * 2];
        /* The memory required by cycle_map cannot exceed 2 times row*col by design*/

        get_cycles(cycle_map, num_reduced_row, num_reduced_col);

        size_t *cycle_stat = new size_t[cycle_map[0] * 2], stat_idx = 0;
        clKernWrite(transKernel, 0) << std::endl;

        clKernWrite(transKernel, 0) << "__constant int swap_table[][3] = {" << std::endl;

        size_t inx = 0, start_inx, swap_inx = 0, num_swaps = 0;
        for (int i = 0; i < cycle_map[0]; i++)
        {
            start_inx = cycle_map[++inx];
            clKernWrite(transKernel, 0) << "{  " << start_inx << ",  " << cycle_map[inx + 1] << ",  0}," << std::endl;
            cycle_stat[stat_idx++] = num_swaps;
            num_swaps++;

            while (start_inx != cycle_map[++inx])
            {
                int action_var = (cycle_map[inx + 1] == start_inx) ? 2 : 1;
                clKernWrite(transKernel, 0) << "{  " << cycle_map[inx] << ",  " << cycle_map[inx + 1] << ",  " << action_var << "}," << std::endl;
                if(action_var == 2)
                    cycle_stat[stat_idx++] = num_swaps;
                num_swaps++;
            }
        }
        /*Appending swap table for touching corner elements for post call back*/
        size_t last_datablk_idx = num_reduced_row * num_reduced_col - 1;
        clKernWrite(transKernel, 0) << "{  0,  0,  0}," << std::endl;
        clKernWrite(transKernel, 0) << "{  "<< last_datablk_idx <<",  " << last_datablk_idx << ",  0}," << std::endl;

        clKernWrite(transKernel, 0) << "};" << std::endl;
        /*cycle_map[0] + 2, + 2 is added for post callback table appending*/
        size_t num_cycles_minus_1 = cycle_map[0] - 1;

        clKernWrite(transKernel, 0) << "__constant int cycle_stat["<< cycle_map[0] <<"][2] = {" << std::endl;
        for (int i = 0; i < num_cycles_minus_1; i++)
        {
            clKernWrite(transKernel, 0) << "{  " << cycle_stat[i * 2] << ",  " << cycle_stat[i * 2 + 1] << "}," << std::endl;
        }
        clKernWrite(transKernel, 0) << "{  " << cycle_stat[num_cycles_minus_1 * 2] << ",  " << (cycle_stat[num_cycles_minus_1 * 2 + 1] + 2)<< "}," << std::endl;

        clKernWrite(transKernel, 0) << "};" << std::endl;

        clKernWrite(transKernel, 0) << std::endl;

        switch (params.fft_inputLayout)
        {
        case CLFFT_COMPLEX_INTERLEAVED:
            clKernWrite(transKernel, 0) << "void swap(global " << dtComplex << "* inputA, " << tmpBuffType << " " << dtComplex << "* Ls, "<< tmpBuffType << " " << dtComplex << " * Ld, int is, int id, int pos, int end_indx, int work_id";
            break;
        case CLFFT_COMPLEX_PLANAR:
            clKernWrite(transKernel, 0) << "void swap(global " << dtPlanar << "* inputA_R, global " << dtPlanar << "* inputA_I, " << tmpBuffType << " " <<dtComplex << "* Ls, "<< tmpBuffType << " " << dtComplex << "* Ld, int is, int id, int pos, int end_indx, int work_id";
            break;
        case CLFFT_HERMITIAN_INTERLEAVED:
        case CLFFT_HERMITIAN_PLANAR:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        case CLFFT_REAL:
            clKernWrite(transKernel, 0) << "void swap(global " << dtPlanar << "* inputA, " << tmpBuffType <<" " << dtPlanar << "* Ls, "<< tmpBuffType <<" " << dtPlanar << "* Ld, int is, int id, int pos, int end_indx, int work_id";
            break;
        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }

		if (params.fft_hasPostCallback)
		{
			clKernWrite(transKernel, 0) << ", size_t iOffset, __global void* post_userdata";
			if (params.fft_postCallback.localMemSize > 0)
			{
				clKernWrite(transKernel, 0) << ", __local void* localmem";
			}
		}

		clKernWrite(transKernel, 0) << "){" << std::endl;

        clKernWrite(transKernel, 3) << "for (int j = get_local_id(0); j < end_indx; j += " << local_work_size_swap << "){" << std::endl;

        switch (params.fft_inputLayout)
        {
        case CLFFT_REAL:
        case CLFFT_COMPLEX_INTERLEAVED:

			
            clKernWrite(transKernel, 6) << "if (pos == 0){" << std::endl;
            clKernWrite(transKernel, 9) << "Ls[j] = inputA[is *" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 9) << "Ld[j] = inputA[id *" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 6) << "}" << std::endl;

            clKernWrite(transKernel, 6) << "else if (pos == 1){" << std::endl;
            clKernWrite(transKernel, 9) << "Ld[j] = inputA[id *" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 6) << "}" << std::endl;

			if (params.fft_hasPostCallback)
			{	
				clKernWrite(transKernel, 6) << params.fft_postCallback.funcname << "(inputA, (iOffset + id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j), post_userdata, Ls[j]";
				if (params.fft_postCallback.localMemSize > 0)
				{
					clKernWrite( transKernel, 0 ) << ", localmem";
				}
				clKernWrite( transKernel, 0 ) << ");" << std::endl;
			}
			else
			{
				clKernWrite(transKernel, 6) << "inputA[id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j] = Ls[j];" << std::endl;
			}
            break;
        case CLFFT_HERMITIAN_INTERLEAVED:
        case CLFFT_HERMITIAN_PLANAR:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        case CLFFT_COMPLEX_PLANAR:
			
            clKernWrite(transKernel, 6) << "if (pos == 0){" << std::endl;
            clKernWrite(transKernel, 9) << "Ls[j].x = inputA_R[is*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 9) << "Ls[j].y = inputA_I[is*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 9) << "Ld[j].x = inputA_R[id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 9) << "Ld[j].y = inputA_I[id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 6) << "}" << std::endl;

            clKernWrite(transKernel, 6) << "else if (pos == 1){" << std::endl;
            clKernWrite(transKernel, 9) << "Ld[j].x = inputA_R[id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 9) << "Ld[j].y = inputA_I[id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j];" << std::endl;
            clKernWrite(transKernel, 6) << "}" << std::endl;

			if (params.fft_hasPostCallback)
			{
				clKernWrite(transKernel, 6) << params.fft_postCallback.funcname << "(inputA_R, inputA_I, (iOffset + id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j), post_userdata, Ls[j].x, Ls[j].y";
				if (params.fft_postCallback.localMemSize > 0)
				{
					clKernWrite( transKernel, 0 ) << ", localmem";
				}
				clKernWrite( transKernel, 0 ) << ");" << std::endl;
			}
			else
			{
				clKernWrite(transKernel, 6) << "inputA_R[id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j] = Ls[j].x;" << std::endl;
				clKernWrite(transKernel, 6) << "inputA_I[id*" << smaller_dim << " + " << num_elements_loaded << " * work_id + j] = Ls[j].y;" << std::endl;
			}
            break;
        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }
        clKernWrite(transKernel, 3) << "}" << std::endl;

        clKernWrite(transKernel, 0) << "}" << std::endl << std::endl;

        funcName = "swap_nonsquare";
        // Generate kernel API

        /*when swap can be performed in LDS itself then, same prototype of transpose can be used for swap function too*/
        genTransposePrototype(params, local_work_size_swap, dtPlanar, dtComplex, funcName, transKernel, dtInput, dtOutput);

        clKernWrite(transKernel, 3) << "size_t g_index = get_group_id(0);" << std::endl;

        clKernWrite(transKernel, 3) << "const size_t numGroupsY_1 = "<< cycle_map[0] * num_grps_pro_row<<" ;" << std::endl;
        for (int i = 2; i < params.fft_DataDim - 1; i++)
        {
            clKernWrite(transKernel, 3) << "const size_t numGroupsY_" << i << " = numGroupsY_" << i - 1 << " * " << params.fft_N[i] << ";" << std::endl;
        }

        delete[] cycle_map;
        delete[] cycle_stat;

        Swap_OffsetCalc(transKernel, params);

        // Handle planar and interleaved right here
        switch (params.fft_inputLayout)
        {
        case CLFFT_COMPLEX_INTERLEAVED:
        case CLFFT_REAL:

            clKernWrite(transKernel, 3) << "__local " << dtInput << " tmp_tot_mem[" << (num_elements_loaded * 2) << "];" << std::endl;
            clKernWrite(transKernel, 3) << tmpBuffType <<" " << dtInput << " *te = tmp_tot_mem;" << std::endl;

            clKernWrite(transKernel, 3) << tmpBuffType <<" " << dtInput << " *to = (tmp_tot_mem + " << num_elements_loaded << ");" << std::endl;
			 
			//Do not advance offset when postcallback is set as the starting address of global buffer is needed
            if (!params.fft_hasPostCallback)
                clKernWrite(transKernel, 3) << "inputA += iOffset;" << std::endl;  // Set A ptr to the start of each slice
            break;
        case CLFFT_COMPLEX_PLANAR:
           
            clKernWrite(transKernel, 3) << "__local " << dtComplex << " tmp_tot_mem[" << (num_elements_loaded * 2) << "];" << std::endl;
            clKernWrite(transKernel, 3) << tmpBuffType << " " << dtComplex << " *te = tmp_tot_mem;" << std::endl;

            clKernWrite(transKernel, 3) << tmpBuffType << " " << dtComplex << " *to = (tmp_tot_mem + " << num_elements_loaded << ");" << std::endl;

			//Do not advance offset when postcallback is set as the starting address of global buffer is needed
            if (!params.fft_hasPostCallback)
            {
                clKernWrite(transKernel, 3) << "inputA_R += iOffset;" << std::endl;  // Set A ptr to the start of each slice 
                clKernWrite(transKernel, 3) << "inputA_I += iOffset;" << std::endl;  // Set A ptr to the start of each slice 
            }
            break;
        case CLFFT_HERMITIAN_INTERLEAVED:
        case CLFFT_HERMITIAN_PLANAR:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;

        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }

        switch (params.fft_inputLayout)
        {
        case CLFFT_COMPLEX_INTERLEAVED:
        case CLFFT_COMPLEX_PLANAR:
            clKernWrite(transKernel, 3) << tmpBuffType << " " << dtComplex << " *tmp_swap_ptr[2];" << std::endl;
            break;
        case CLFFT_REAL:
            clKernWrite(transKernel, 3) << tmpBuffType << " " << dtPlanar << " *tmp_swap_ptr[2];" << std::endl;
        }
        clKernWrite(transKernel, 3) << "tmp_swap_ptr[0] = te;" << std::endl;
        clKernWrite(transKernel, 3) << "tmp_swap_ptr[1] = to;" << std::endl;

        clKernWrite(transKernel, 3) << "int swap_inx = 0;" << std::endl;

        clKernWrite(transKernel, 3) << "int start = cycle_stat[g_index / "<< num_grps_pro_row <<"][0];" << std::endl;
        clKernWrite(transKernel, 3) << "int end = cycle_stat[g_index / "<< num_grps_pro_row <<"][1];" << std::endl;

        clKernWrite(transKernel, 3) << "int end_indx = "<< num_elements_loaded <<";" << std::endl;
        clKernWrite(transKernel, 3) << "int work_id = g_index % " << num_grps_pro_row << ";" << std::endl;

        clKernWrite(transKernel, 3) << "if( work_id == "<< (num_grps_pro_row - 1) <<" ){" << std::endl;
        clKernWrite(transKernel, 6) << "end_indx = " << smaller_dim - num_elements_loaded * (num_grps_pro_row - 1) << ";" << std::endl;
        clKernWrite(transKernel, 3) << "}" << std::endl;

        clKernWrite(transKernel, 3) << "for (int loop = start; loop <= end; loop ++){" << std::endl;
        clKernWrite(transKernel, 6) << "swap_inx = 1 - swap_inx;" << std::endl;

        switch (params.fft_inputLayout)
        {
        case CLFFT_COMPLEX_INTERLEAVED:
        case CLFFT_REAL:
            clKernWrite(transKernel, 6) << "swap(inputA, tmp_swap_ptr[swap_inx], tmp_swap_ptr[1 - swap_inx], swap_table[loop][0], swap_table[loop][1], swap_table[loop][2], end_indx, work_id";
            break;
        case CLFFT_COMPLEX_PLANAR:
            clKernWrite(transKernel, 6) << "swap(inputA_R, inputA_I, tmp_swap_ptr[swap_inx], tmp_swap_ptr[1 - swap_inx], swap_table[loop][0], swap_table[loop][1], swap_table[loop][2], end_indx, work_id";
            break;
        }
		if (params.fft_hasPostCallback)
		{
			clKernWrite(transKernel, 0) << ", iOffset, post_userdata";
			if (params.fft_postCallback.localMemSize > 0)
			{
				clKernWrite(transKernel, 0) << ", localmem";
			}
		}
		clKernWrite(transKernel, 0) << ");" << std::endl;

        clKernWrite(transKernel, 3) << "}" << std::endl;

        clKernWrite(transKernel, 0) << "}" << std::endl;
        strKernel = transKernel.str();
    }
    return CLFFT_SUCCESS;
}

static clfftStatus genTransposeKernel(const FFTGeneratedTransposeNonSquareAction::Signature & params, std::string& strKernel, const size_t& lwSize, const size_t reShapeFactor)
{
    strKernel.reserve(4096);
    std::stringstream transKernel(std::stringstream::out);

    // These strings represent the various data types we read or write in the kernel, depending on how the plan
    // is configured
    std::string dtInput;        // The type read as input into kernel
    std::string dtOutput;       // The type written as output from kernel
    std::string dtPlanar;       // Fundamental type for planar arrays
    std::string dtComplex;      // Fundamental type for complex arrays

                                // NOTE:  Enable only for debug
                                // clKernWrite( transKernel, 0 ) << "#pragma OPENCL EXTENSION cl_amd_printf : enable\n" << std::endl;

                                //if (params.fft_inputLayout != params.fft_outputLayout)
                                //	return CLFFT_TRANSPOSED_NOTIMPLEMENTED;

    switch (params.fft_precision)
    {
    case CLFFT_SINGLE:
    case CLFFT_SINGLE_FAST:
        dtPlanar = "float";
        dtComplex = "float2";
        break;
    case CLFFT_DOUBLE:
    case CLFFT_DOUBLE_FAST:
        dtPlanar = "double";
        dtComplex = "double2";

        // Emit code that enables double precision in the kernel
        clKernWrite(transKernel, 0) << "#ifdef cl_khr_fp64" << std::endl;
        clKernWrite(transKernel, 3) << "#pragma OPENCL EXTENSION cl_khr_fp64 : enable" << std::endl;
        clKernWrite(transKernel, 0) << "#else" << std::endl;
        clKernWrite(transKernel, 3) << "#pragma OPENCL EXTENSION cl_amd_fp64 : enable" << std::endl;
        clKernWrite(transKernel, 0) << "#endif\n" << std::endl;

        break;
    default:
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        break;
    }


    //	If twiddle computation has been requested, generate the lookup function
    if (params.fft_3StepTwiddle)
    {
        std::string str;
        StockhamGenerator::TwiddleTableLarge twLarge(params.fft_N[0] * params.fft_N[1]);
        if ((params.fft_precision == CLFFT_SINGLE) || (params.fft_precision == CLFFT_SINGLE_FAST))
            twLarge.GenerateTwiddleTable<StockhamGenerator::P_SINGLE>(str);
        else
            twLarge.GenerateTwiddleTable<StockhamGenerator::P_DOUBLE>(str);
        clKernWrite(transKernel, 0) << str << std::endl;
        clKernWrite(transKernel, 0) << std::endl;
    }


    // This detects whether the input matrix is rectangle of ratio 1:2
    
    if ((params.fft_N[0] != 2 * params.fft_N[1]) && (params.fft_N[1] != 2 * params.fft_N[0]))
    {
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    }

    if (params.fft_placeness == CLFFT_OUTOFPLACE)
    {
        return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
    }

    size_t smaller_dim = (params.fft_N[0] < params.fft_N[1]) ? params.fft_N[0] : params.fft_N[1];

    // This detects whether the input matrix is a multiple of 16*reshapefactor or not

    bool mult_of_16 = (smaller_dim % (reShapeFactor * 16) == 0) ? true : false;

    for (size_t bothDir = 0; bothDir < 2; bothDir++)
    {
        bool fwd = bothDir ? false : true;

        //If pre-callback is set for the plan
        if (params.fft_hasPreCallback)
        {
            //Insert callback function code at the beginning 
            clKernWrite(transKernel, 0) << params.fft_preCallback.funcstring << std::endl;
            clKernWrite(transKernel, 0) << std::endl;
        }

        std::string funcName;
        if (params.fft_3StepTwiddle) // TODO
            funcName = fwd ? "transpose_nonsquare_tw_fwd" : "transpose_nonsquare_tw_back";
        else
            funcName = "transpose_nonsquare";


        // Generate kernel API
        genTransposePrototype(params, lwSize, dtPlanar, dtComplex, funcName, transKernel, dtInput, dtOutput);

        if (mult_of_16)
            clKernWrite(transKernel, 3) << "const int numGroups_square_matrix_Y_1 = " << (smaller_dim / 16 / reShapeFactor)*(smaller_dim / 16 / reShapeFactor + 1) / 2 << ";" << std::endl;
        else
            clKernWrite(transKernel, 3) << "const int numGroups_square_matrix_Y_1 = " << (smaller_dim / (16 * reShapeFactor) + 1)*(smaller_dim / (16 * reShapeFactor) + 1 + 1) / 2 << ";" << std::endl;

        clKernWrite(transKernel, 3) << "const int numGroupsY_1 =  numGroups_square_matrix_Y_1 * 2 ;" << std::endl;

        for (int i = 2; i < params.fft_DataDim - 1; i++)
        {
            clKernWrite(transKernel, 3) << "const size_t numGroupsY_" << i << " = numGroupsY_" << i - 1 << " * " << params.fft_N[i] << ";" << std::endl;
        }

        clKernWrite(transKernel, 3) << "size_t g_index;" << std::endl;
        clKernWrite(transKernel, 3) << "size_t square_matrix_index;" << std::endl;
        clKernWrite(transKernel, 3) << "size_t square_matrix_offset;" << std::endl;
        clKernWrite(transKernel, 3) << std::endl;

        OffsetCalc(transKernel, params);

        clKernWrite(transKernel, 3) << "square_matrix_index = (g_index / numGroups_square_matrix_Y_1) ;" << std::endl;
        clKernWrite(transKernel, 3) << "g_index = g_index % numGroups_square_matrix_Y_1" << ";" << std::endl;
        clKernWrite(transKernel, 3) << std::endl;

        if (smaller_dim == params.fft_N[1])
        {
            clKernWrite(transKernel, 3) << "square_matrix_offset = square_matrix_index * " << smaller_dim <<";" << std::endl;
        }
        else
        {
            clKernWrite(transKernel, 3) << "square_matrix_offset = square_matrix_index *" << smaller_dim * smaller_dim <<";" << std::endl;
        }

        clKernWrite(transKernel, 3) << "iOffset += square_matrix_offset ;" << std::endl;

        // Handle planar and interleaved right here
        switch (params.fft_inputLayout)
        {
        case CLFFT_COMPLEX_INTERLEAVED:
        case CLFFT_REAL:
            //Do not advance offset when precallback is set as the starting address of global buffer is needed
            if (!params.fft_hasPreCallback)
            {
                clKernWrite(transKernel, 3) << "inputA += iOffset;" << std::endl;  // Set A ptr to the start of each slice
            }
            break;
        case CLFFT_COMPLEX_PLANAR:
            //Do not advance offset when precallback is set as the starting address of global buffer is needed
            if (!params.fft_hasPreCallback)
            {
                clKernWrite(transKernel, 3) << "inputA_R += iOffset;" << std::endl;  // Set A ptr to the start of each slice 
                clKernWrite(transKernel, 3) << "inputA_I += iOffset;" << std::endl;  // Set A ptr to the start of each slice 
            }
            break;
        case CLFFT_HERMITIAN_INTERLEAVED:
        case CLFFT_HERMITIAN_PLANAR:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }
        
        switch (params.fft_inputLayout)
        {
        case CLFFT_COMPLEX_INTERLEAVED:
        case CLFFT_REAL:
            if (params.fft_hasPreCallback)
            {
                clKernWrite(transKernel, 3) << "global " << dtInput << " *outputA = inputA + iOffset;" << std::endl;
            }
            else
            {
                clKernWrite(transKernel, 3) << "global " << dtInput << " *outputA = inputA;" << std::endl;
            }
            break;
        case CLFFT_COMPLEX_PLANAR:
            if (params.fft_hasPreCallback)
            {
                clKernWrite(transKernel, 3) << "global " << dtInput << " *outputA_R = inputA_R + iOffset;" << std::endl;
                clKernWrite(transKernel, 3) << "global " << dtInput << " *outputA_I = inputA_I + iOffset;" << std::endl;
            }
            else
            {
                clKernWrite(transKernel, 3) << "global " << dtInput << " *outputA_R = inputA_R;" << std::endl;
                clKernWrite(transKernel, 3) << "global " << dtInput << " *outputA_I = inputA_I;" << std::endl;
            }
            break;
        case CLFFT_HERMITIAN_INTERLEAVED:
        case CLFFT_HERMITIAN_PLANAR:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }
        

        clKernWrite(transKernel, 3) << std::endl;

        // Now compute the corresponding y,x coordinates
        // for a triangular indexing
        if (mult_of_16)
            clKernWrite(transKernel, 3) << "float row = (" << -2.0f*smaller_dim / 16 / reShapeFactor - 1 << "+sqrt((" << 4.0f*smaller_dim / 16 / reShapeFactor*(smaller_dim / 16 / reShapeFactor + 1) << "-8.0f*g_index- 7)))/ (-2.0f);" << std::endl;
        else
            clKernWrite(transKernel, 3) << "float row = (" << -2.0f*(smaller_dim / (16 * reShapeFactor) + 1) - 1 << "+sqrt((" << 4.0f*(smaller_dim / (16 * reShapeFactor) + 1)*(smaller_dim / (16 * reShapeFactor) + 1 + 1) << "-8.0f*g_index- 7)))/ (-2.0f);" << std::endl;


        clKernWrite(transKernel, 3) << "if (row == (float)(int)row) row -= 1; " << std::endl;
        clKernWrite(transKernel, 3) << "const int t_gy = (int)row;" << std::endl;

        clKernWrite(transKernel, 3) << "" << std::endl;

        if (mult_of_16)
            clKernWrite(transKernel, 3) << "const int t_gx_p = g_index - " << (smaller_dim / 16 / reShapeFactor) << "*t_gy + t_gy*(t_gy + 1) / 2;" << std::endl;
        else
            clKernWrite(transKernel, 3) << "const int t_gx_p = g_index - " << (smaller_dim / (16 * reShapeFactor) + 1) << "*t_gy + t_gy*(t_gy + 1) / 2;" << std::endl;

        clKernWrite(transKernel, 3) << "const int t_gy_p = t_gx_p - t_gy;" << std::endl;


        clKernWrite(transKernel, 3) << "" << std::endl;

        clKernWrite(transKernel, 3) << "const int d_lidx = get_local_id(0) % 16;" << std::endl;
        clKernWrite(transKernel, 3) << "const int d_lidy = get_local_id(0) / 16;" << std::endl;

        clKernWrite(transKernel, 3) << "" << std::endl;

        clKernWrite(transKernel, 3) << "const int lidy = (d_lidy * 16 + d_lidx) /" << (16 * reShapeFactor) << ";" << std::endl;
        clKernWrite(transKernel, 3) << "const int lidx = (d_lidy * 16 + d_lidx) %" << (16 * reShapeFactor) << ";" << std::endl;

        clKernWrite(transKernel, 3) << "" << std::endl;

        clKernWrite(transKernel, 3) << "const int idx = lidx + t_gx_p*" << 16 * reShapeFactor << ";" << std::endl;
        clKernWrite(transKernel, 3) << "const int idy = lidy + t_gy_p*" << 16 * reShapeFactor << ";" << std::endl;

        clKernWrite(transKernel, 3) << "" << std::endl;

        clKernWrite(transKernel, 3) << "const int starting_index_yx = t_gy_p*" << 16 * reShapeFactor << " + t_gx_p*" << 16 * reShapeFactor*params.fft_N[0] << ";" << std::endl;

        clKernWrite(transKernel, 3) << "" << std::endl;

        switch (params.fft_inputLayout)
        {
        case CLFFT_REAL:
        case CLFFT_COMPLEX_INTERLEAVED:
            clKernWrite(transKernel, 3) << "__local " << dtInput << " xy_s[" << 16 * reShapeFactor * 16 * reShapeFactor << "];" << std::endl;
            clKernWrite(transKernel, 3) << "__local " << dtInput << " yx_s[" << 16 * reShapeFactor * 16 * reShapeFactor << "];" << std::endl;

            clKernWrite(transKernel, 3) << dtInput << " tmpm, tmpt;" << std::endl;
            break;        
        case CLFFT_COMPLEX_PLANAR:
            clKernWrite(transKernel, 3) << "__local " << dtComplex << " xy_s[" << 16 * reShapeFactor * 16 * reShapeFactor << "];" << std::endl;
            clKernWrite(transKernel, 3) << "__local " << dtComplex << " yx_s[" << 16 * reShapeFactor * 16 * reShapeFactor << "];" << std::endl;

            clKernWrite(transKernel, 3) << dtComplex << " tmpm, tmpt;" << std::endl;
            break;
        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }
        clKernWrite(transKernel, 3) << "" << std::endl;

        // Step 1: Load both blocks into local memory
        // Here I load inputA for both blocks contiguously and write it contigously into
        // the corresponding shared memories.
        // Afterwards I use non-contiguous access from local memory and write contiguously
        // back into the arrays

        if (mult_of_16) {
            clKernWrite(transKernel, 3) << "int index;" << std::endl;
            clKernWrite(transKernel, 3) << "for (int loop = 0; loop<" << reShapeFactor*reShapeFactor << "; ++loop){" << std::endl;
            clKernWrite(transKernel, 6) << "index = lidy*" << 16 * reShapeFactor << " + lidx + loop*256;" << std::endl;

            // Handle planar and interleaved right here
            switch (params.fft_inputLayout)
            {
            case CLFFT_COMPLEX_INTERLEAVED:
            case CLFFT_REAL:
            {
                if (params.fft_hasPreCallback)
                {
                    if (params.fft_preCallback.localMemSize > 0)
                    {
                        clKernWrite(transKernel, 6) << "tmpm = " << params.fft_preCallback.funcname << "(inputA, iOffset + (idy + loop * " << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata, localmem);" << std::endl;
                        clKernWrite(transKernel, 6) << "tmpt = " << params.fft_preCallback.funcname << "(inputA, iOffset + (lidy + loop * " << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata, localmem);" << std::endl;
                    }
                    else
                    {
                        clKernWrite(transKernel, 6) << "tmpm = " << params.fft_preCallback.funcname << "(inputA, iOffset + (idy + loop * " << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata);" << std::endl;
                        clKernWrite(transKernel, 6) << "tmpt = " << params.fft_preCallback.funcname << "(inputA, iOffset + (lidy + loop * " << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata);" << std::endl;
                    }
                }
                else
                {
                    clKernWrite(transKernel, 6) << "tmpm = inputA[(idy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;
                    clKernWrite(transKernel, 6) << "tmpt = inputA[(lidy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                }
            }
            break;
            case CLFFT_COMPLEX_PLANAR:
                dtInput = dtPlanar;
                dtOutput = dtPlanar;
                if (params.fft_hasPreCallback)
                {
                    if (params.fft_preCallback.localMemSize > 0)
                    {
                        clKernWrite(transKernel, 6) << "tmpm = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (idy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata, localmem);" << std::endl;
                        clKernWrite(transKernel, 6) << "tmpt = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (lidy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata, localmem);" << std::endl;
                    }
                    else
                    {
                        clKernWrite(transKernel, 6) << "tmpm = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (idy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata);" << std::endl;
                        clKernWrite(transKernel, 6) << "tmpt = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (lidy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata);" << std::endl;
                    }
                }
                else
                {
                    clKernWrite(transKernel, 6) << "tmpm.x = inputA_R[(idy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;
                    clKernWrite(transKernel, 6) << "tmpm.y = inputA_I[(idy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;

                    clKernWrite(transKernel, 6) << "tmpt.x = inputA_R[(lidy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                    clKernWrite(transKernel, 6) << "tmpt.y = inputA_I[(lidy + loop *" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                }
                break;
            case CLFFT_HERMITIAN_INTERLEAVED:
            case CLFFT_HERMITIAN_PLANAR:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            default:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            }

            // If requested, generate the Twiddle math to multiply constant values
            if (params.fft_3StepTwiddle)
                    genTwiddleMath(params, transKernel, dtComplex, fwd);

            clKernWrite(transKernel, 6) << "xy_s[index] = tmpm; " << std::endl;
            clKernWrite(transKernel, 6) << "yx_s[index] = tmpt; " << std::endl;

            clKernWrite(transKernel, 3) << "}" << std::endl;

            clKernWrite(transKernel, 3) << "" << std::endl;

            clKernWrite(transKernel, 3) << "barrier(CLK_LOCAL_MEM_FENCE);" << std::endl;

            clKernWrite(transKernel, 3) << "" << std::endl;


            // Step2: Write from shared to global
            clKernWrite(transKernel, 3) << "for (int loop = 0; loop<" << reShapeFactor*reShapeFactor << "; ++loop){" << std::endl;
            clKernWrite(transKernel, 6) << "index = lidx*" << 16 * reShapeFactor << " + lidy + " << 16 / reShapeFactor << "*loop;" << std::endl;


            // Handle planar and interleaved right here
            switch (params.fft_outputLayout)
            {
            case CLFFT_COMPLEX_INTERLEAVED:
            case CLFFT_REAL:
					clKernWrite(transKernel, 6) << "outputA[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index];" << std::endl;
					clKernWrite(transKernel, 6) << "outputA[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx+ starting_index_yx] = xy_s[index];" << std::endl;

                break;
            case CLFFT_COMPLEX_PLANAR:

					clKernWrite(transKernel, 6) << "outputA_R[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index].x;" << std::endl;
					clKernWrite(transKernel, 6) << "outputA_I[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index].y;" << std::endl;

					clKernWrite(transKernel, 6) << "outputA_R[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx+ starting_index_yx] = xy_s[index].x;" << std::endl;
					clKernWrite(transKernel, 6) << "outputA_I[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx+ starting_index_yx] = xy_s[index].y;" << std::endl;
                break;
            case CLFFT_HERMITIAN_INTERLEAVED:
            case CLFFT_HERMITIAN_PLANAR:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            default:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            }



            clKernWrite(transKernel, 3) << "}" << std::endl;

        }
        else {

            clKernWrite(transKernel, 3) << "int index;" << std::endl;
            clKernWrite(transKernel, 3) << "if (" << smaller_dim << " - (t_gx_p + 1) *" << 16 * reShapeFactor << ">0){" << std::endl;
            clKernWrite(transKernel, 6) << "for (int loop = 0; loop<" << reShapeFactor*reShapeFactor << "; ++loop){" << std::endl;
            clKernWrite(transKernel, 9) << "index = lidy*" << 16 * reShapeFactor << " + lidx + loop*256;" << std::endl;

            // Handle planar and interleaved right here
            switch (params.fft_inputLayout)
            {
            case CLFFT_COMPLEX_INTERLEAVED:
            case CLFFT_REAL:
                if (params.fft_hasPreCallback)
                {
                    if (params.fft_preCallback.localMemSize > 0)
                    {
                        clKernWrite(transKernel, 9) << "tmpm = " << params.fft_preCallback.funcname << "(inputA, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata, localmem);" << std::endl;
                        clKernWrite(transKernel, 9) << "tmpt = " << params.fft_preCallback.funcname << "(inputA, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata, localmem);" << std::endl;
                    }
                    else
                    {
                        clKernWrite(transKernel, 9) << "tmpm = " << params.fft_preCallback.funcname << "(inputA, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata);" << std::endl;
                        clKernWrite(transKernel, 9) << "tmpt = " << params.fft_preCallback.funcname << "(inputA, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata);" << std::endl;
                    }
                }
                else
                {
                    clKernWrite(transKernel, 9) << "tmpm = inputA[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;
                    clKernWrite(transKernel, 9) << "tmpt = inputA[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                }
                break;
            case CLFFT_COMPLEX_PLANAR:
                dtInput = dtPlanar;
                dtOutput = dtPlanar;
                if (params.fft_hasPreCallback)
                {
                    if (params.fft_preCallback.localMemSize > 0)
                    {
                        clKernWrite(transKernel, 9) << "tmpm = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata, localmem);" << std::endl;
                        clKernWrite(transKernel, 9) << "tmpt = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata, localmem);" << std::endl;
                    }
                    else
                    {
                        clKernWrite(transKernel, 9) << "tmpm = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata);" << std::endl;
                        clKernWrite(transKernel, 9) << "tmpt = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata);" << std::endl;
                    }
                }
                else
                {
                    clKernWrite(transKernel, 9) << "tmpm.x = inputA_R[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;
                    clKernWrite(transKernel, 9) << "tmpm.y = inputA_I[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;

                    clKernWrite(transKernel, 9) << "tmpt.x = inputA_R[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                    clKernWrite(transKernel, 9) << "tmpt.y = inputA_I[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                }
                break;
            case CLFFT_HERMITIAN_INTERLEAVED:
            case CLFFT_HERMITIAN_PLANAR:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            default:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            }

            // If requested, generate the Twiddle math to multiply constant values
            if (params.fft_3StepTwiddle)
                genTwiddleMath(params, transKernel, dtComplex, fwd);

            clKernWrite(transKernel, 9) << "xy_s[index] = tmpm;" << std::endl;
            clKernWrite(transKernel, 9) << "yx_s[index] = tmpt;" << std::endl;
            clKernWrite(transKernel, 6) << "}" << std::endl;
            clKernWrite(transKernel, 3) << "}" << std::endl;

            clKernWrite(transKernel, 3) << "else{" << std::endl;
            clKernWrite(transKernel, 6) << "for (int loop = 0; loop<" << reShapeFactor*reShapeFactor << "; ++loop){" << std::endl;
            clKernWrite(transKernel, 9) << "index = lidy*" << 16 * reShapeFactor << " + lidx + loop*256;" << std::endl;


            // Handle planar and interleaved right here
            switch (params.fft_inputLayout)
            {
            case CLFFT_COMPLEX_INTERLEAVED:
            case CLFFT_REAL:
                clKernWrite(transKernel, 9) << "if ((idy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << "&& idx<" << smaller_dim << ")" << std::endl;
                if (params.fft_hasPreCallback)
                {
                    if (params.fft_preCallback.localMemSize > 0)
                    {
                        clKernWrite(transKernel, 12) << "tmpm = " << params.fft_preCallback.funcname << "(inputA, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata, localmem);" << std::endl;
                        clKernWrite(transKernel, 9) << "if ((t_gy_p *" << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ") " << std::endl;
                        clKernWrite(transKernel, 12) << "tmpt = " << params.fft_preCallback.funcname << "(inputA, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata, localmem);" << std::endl;
                    }
                    else
                    {
                        clKernWrite(transKernel, 12) << "tmpm = " << params.fft_preCallback.funcname << "(inputA, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata);" << std::endl;
                        clKernWrite(transKernel, 9) << "if ((t_gy_p *" << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ") " << std::endl;
                        clKernWrite(transKernel, 12) << "tmpt = " << params.fft_preCallback.funcname << "(inputA, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata);" << std::endl;
                    }
                }
                else
                {
                    clKernWrite(transKernel, 12) << "tmpm = inputA[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;
                    clKernWrite(transKernel, 9) << "if ((t_gy_p *" << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ") " << std::endl;
                    clKernWrite(transKernel, 12) << "tmpt = inputA[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                }
                break;
            case CLFFT_COMPLEX_PLANAR:
                dtInput = dtPlanar;
                dtOutput = dtPlanar;
                clKernWrite(transKernel, 9) << "if ((idy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << "&& idx<" << smaller_dim << ") {" << std::endl;
                if (params.fft_hasPreCallback)
                {
                    if (params.fft_preCallback.localMemSize > 0)
                    {
                        clKernWrite(transKernel, 12) << "tmpm = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata, localmem); }" << std::endl;
                        clKernWrite(transKernel, 9) << "if ((t_gy_p *" << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ") {" << std::endl;
                        clKernWrite(transKernel, 12) << "tmpt = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata, localmem); }" << std::endl;
                    }
                    else
                    {
                        clKernWrite(transKernel, 12) << "tmpm = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx, pre_userdata); }" << std::endl;
                        clKernWrite(transKernel, 9) << "if ((t_gy_p *" << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ") {" << std::endl;
                        clKernWrite(transKernel, 12) << "tmpt = " << params.fft_preCallback.funcname << "(inputA_R, inputA_I, iOffset + (lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx, pre_userdata); }" << std::endl;
                    }
                }
                else
                {
                    clKernWrite(transKernel, 12) << "tmpm.x = inputA_R[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx];" << std::endl;
                    clKernWrite(transKernel, 12) << "tmpm.y = inputA_I[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx]; }" << std::endl;
                    clKernWrite(transKernel, 9) << "if ((t_gy_p *" << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ") {" << std::endl;
                    clKernWrite(transKernel, 12) << "tmpt.x = inputA_R[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx];" << std::endl;
                    clKernWrite(transKernel, 12) << "tmpt.y = inputA_I[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx]; }" << std::endl;
                }
                break;
            case CLFFT_HERMITIAN_INTERLEAVED:
            case CLFFT_HERMITIAN_PLANAR:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            default:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            }


            // If requested, generate the Twiddle math to multiply constant values
            if (params.fft_3StepTwiddle)
            genTwiddleMath(params, transKernel, dtComplex, fwd);

            clKernWrite(transKernel, 9) << "xy_s[index] = tmpm;" << std::endl;
            clKernWrite(transKernel, 9) << "yx_s[index] = tmpt;" << std::endl;

            clKernWrite(transKernel, 9) << "}" << std::endl;
            clKernWrite(transKernel, 3) << "}" << std::endl;

            clKernWrite(transKernel, 3) << "" << std::endl;
            clKernWrite(transKernel, 3) << "barrier(CLK_LOCAL_MEM_FENCE);" << std::endl;
            clKernWrite(transKernel, 3) << "" << std::endl;

            // Step2: Write from shared to global

            clKernWrite(transKernel, 3) << "if (" << smaller_dim << " - (t_gx_p + 1) *" << 16 * reShapeFactor << ">0){" << std::endl;
            clKernWrite(transKernel, 6) << "for (int loop = 0; loop<" << reShapeFactor*reShapeFactor << "; ++loop){" << std::endl;
            clKernWrite(transKernel, 9) << "index = lidx*" << 16 * reShapeFactor << " + lidy + " << 16 / reShapeFactor << "*loop ;" << std::endl;

            // Handle planar and interleaved right here
            switch (params.fft_outputLayout)
            {
            case CLFFT_COMPLEX_INTERLEAVED:
            case CLFFT_REAL:
                clKernWrite(transKernel, 9) << "outputA[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index];" << std::endl;
                clKernWrite(transKernel, 9) << "outputA[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx] = xy_s[index]; " << std::endl;

                break;
            case CLFFT_COMPLEX_PLANAR:
                clKernWrite(transKernel, 9) << "outputA_R[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index].x;" << std::endl;
                clKernWrite(transKernel, 9) << "outputA_I[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index].y;" << std::endl;
                clKernWrite(transKernel, 9) << "outputA_R[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx] = xy_s[index].x; " << std::endl;
                clKernWrite(transKernel, 9) << "outputA_I[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx] = xy_s[index].y; " << std::endl;



                break;
            case CLFFT_HERMITIAN_INTERLEAVED:
            case CLFFT_HERMITIAN_PLANAR:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;

            default:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            }


            clKernWrite(transKernel, 6) << "}" << std::endl;
            clKernWrite(transKernel, 3) << "}" << std::endl;

            clKernWrite(transKernel, 3) << "else{" << std::endl;
            clKernWrite(transKernel, 6) << "for (int loop = 0; loop<" << reShapeFactor*reShapeFactor << "; ++loop){" << std::endl;

            clKernWrite(transKernel, 9) << "index = lidx*" << 16 * reShapeFactor << " + lidy + " << 16 / reShapeFactor << "*loop;" << std::endl;

            // Handle planar and interleaved right here
            switch (params.fft_outputLayout)
            {
            case CLFFT_COMPLEX_INTERLEAVED:
            case CLFFT_REAL:
                clKernWrite(transKernel, 9) << "if ((idy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << " && idx<" << smaller_dim << ")" << std::endl;
                clKernWrite(transKernel, 12) << "outputA[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index]; " << std::endl;
                clKernWrite(transKernel, 9) << "if ((t_gy_p * " << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ")" << std::endl;
                clKernWrite(transKernel, 12) << "outputA[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx] = xy_s[index];" << std::endl;

                break;
            case CLFFT_COMPLEX_PLANAR:
                clKernWrite(transKernel, 9) << "if ((idy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << " && idx<" << smaller_dim << ") {" << std::endl;
                clKernWrite(transKernel, 12) << "outputA_R[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index].x; " << std::endl;
                clKernWrite(transKernel, 12) << "outputA_I[(idy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + idx] = yx_s[index].y; }" << std::endl;
                clKernWrite(transKernel, 9) << "if ((t_gy_p * " << 16 * reShapeFactor << " + lidx)<" << smaller_dim << " && (t_gx_p * " << 16 * reShapeFactor << " + lidy + loop*" << 16 / reShapeFactor << ")<" << smaller_dim << ") {" << std::endl;
                clKernWrite(transKernel, 12) << "outputA_R[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx] = xy_s[index].x;" << std::endl;
                clKernWrite(transKernel, 12) << "outputA_I[(lidy + loop*" << 16 / reShapeFactor << ")*" << params.fft_N[0] << " + lidx + starting_index_yx] = xy_s[index].y; }" << std::endl;


                break;
            case CLFFT_HERMITIAN_INTERLEAVED:
            case CLFFT_HERMITIAN_PLANAR:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            default:
                return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
            }


            clKernWrite(transKernel, 6) << "}" << std::endl; // end for
            clKernWrite(transKernel, 3) << "}" << std::endl; // end else

        }
        clKernWrite(transKernel, 0) << "}" << std::endl;

        strKernel = transKernel.str();

        if (!params.fft_3StepTwiddle)
            break;
    }

    return CLFFT_SUCCESS;
}


clfftStatus FFTGeneratedTransposeNonSquareAction::initParams()
{

    this->signature.fft_precision = this->plan->precision;
    this->signature.fft_placeness = this->plan->placeness;
    this->signature.fft_inputLayout = this->plan->inputLayout;
    this->signature.fft_outputLayout = this->plan->outputLayout;
    this->signature.fft_3StepTwiddle = false;
    this->signature.nonSquareKernelType = this->plan->nonSquareKernelType;

    this->signature.fft_realSpecial = this->plan->realSpecial;

    this->signature.transOutHorizontal = this->plan->transOutHorizontal;	// using the twiddle front flag to specify horizontal write
                                                                            // we do this so as to reuse flags in FFTKernelGenKeyParams
                                                                            // and to avoid making a new one 

    ARG_CHECK(this->plan->inStride.size() == this->plan->outStride.size());

    if (CLFFT_INPLACE == this->signature.fft_placeness)
    {
        //	If this is an in-place transform the
        //	input and output layout
        //	*MUST* be the same.
        //
        ARG_CHECK(this->signature.fft_inputLayout == this->signature.fft_outputLayout)

    /*        for (size_t u = this->plan->inStride.size(); u-- > 0; )
            {
                ARG_CHECK(this->plan->inStride[u] == this->plan->outStride[u]);
            }*/
    }

    this->signature.fft_DataDim = this->plan->length.size() + 1;
    int i = 0;
    for (i = 0; i < (this->signature.fft_DataDim - 1); i++)
    {
        this->signature.fft_N[i] = this->plan->length[i];
        this->signature.fft_inStride[i] = this->plan->inStride[i];
        this->signature.fft_outStride[i] = this->plan->outStride[i];

    }
    this->signature.fft_inStride[i] = this->plan->iDist;
    this->signature.fft_outStride[i] = this->plan->oDist;

    if (this->plan->large1D != 0) {
        ARG_CHECK(this->signature.fft_N[0] != 0)
            //ToDo:ENABLE ASSERT
       //     ARG_CHECK((this->plan->large1D % this->signature.fft_N[0]) == 0)
            this->signature.fft_3StepTwiddle = true;
        //ToDo:ENABLE ASSERT
       // ARG_CHECK(this->plan->large1D == (this->signature.fft_N[1] * this->signature.fft_N[0]));
    }

    //	Query the devices in this context for their local memory sizes
    //	How we generate a kernel depends on the *minimum* LDS size for all devices.
    //
    const FFTEnvelope * pEnvelope = NULL;
    OPENCL_V(this->plan->GetEnvelope(&pEnvelope), _T("GetEnvelope failed"));
    BUG_CHECK(NULL != pEnvelope);

    // TODO:  Since I am going with a 2D workgroup size now, I need a better check than this 1D use
    // Check:  CL_DEVICE_MAX_WORK_GROUP_SIZE/CL_KERNEL_WORK_GROUP_SIZE
    // CL_DEVICE_MAX_WORK_ITEM_SIZES
    this->signature.fft_R = 1; // Dont think i'll use
    this->signature.fft_SIMD = pEnvelope->limit_WorkGroupSize; // Use devices maximum workgroup size

                                                               //Set callback if specified
    if (this->plan->hasPreCallback)
    {
        this->signature.fft_hasPreCallback = true;
        this->signature.fft_preCallback = this->plan->preCallback;
    }
	if (this->plan->hasPostCallback)
	{
		this->signature.fft_hasPostCallback = true;
		this->signature.fft_postCallback = this->plan->postCallbackParam;
	}
	this->signature.limit_LocalMemSize = this->plan->envelope.limit_LocalMemSize;

    return CLFFT_SUCCESS;
}


static const size_t lwSize = 256;
static const size_t reShapeFactor = 2;


//	OpenCL does not take unicode strings as input, so this routine returns only ASCII strings
//	Feed this generator the FFTPlan, and it returns the generated program as a string
clfftStatus FFTGeneratedTransposeNonSquareAction::generateKernel(FFTRepo& fftRepo, const cl_command_queue commQueueFFT)
{


    std::string programCode;
    if (this->signature.nonSquareKernelType == NON_SQUARE_TRANS_TRANSPOSE)
    {
		//Requested local memory size by callback must not exceed the device LDS limits after factoring the LDS size required by transpose kernel
		if (this->signature.fft_hasPreCallback && this->signature.fft_preCallback.localMemSize > 0)
		{
			assert(!this->signature.fft_hasPostCallback);

			bool validLDSSize = false;
			size_t requestedCallbackLDS = 0;

			requestedCallbackLDS = this->signature.fft_preCallback.localMemSize;
			
			validLDSSize = ((2 * this->plan->ElementSize() * 16 * reShapeFactor * 16 * reShapeFactor) + requestedCallbackLDS) < this->plan->envelope.limit_LocalMemSize;
		
			if(!validLDSSize)
			{
				fprintf(stderr, "Requested local memory size not available\n");
				return CLFFT_INVALID_ARG_VALUE;
			}
		}
        OPENCL_V(genTransposeKernel(this->signature, programCode, lwSize, reShapeFactor), _T("genTransposeKernel() failed!"));
    }
    else
    {
		//No pre-callback possible in swap kernel
		assert(!this->signature.fft_hasPreCallback);

        OPENCL_V(genSwapKernel(this->signature, programCode, lwSize, reShapeFactor), _T("genSwapKernel() failed!"));
    }

    cl_int status = CL_SUCCESS;
    cl_device_id Device = NULL;
    status = clGetCommandQueueInfo(commQueueFFT, CL_QUEUE_DEVICE, sizeof(cl_device_id), &Device, NULL);
    OPENCL_V(status, _T("clGetCommandQueueInfo failed"));

    cl_context QueueContext = NULL;
    status = clGetCommandQueueInfo(commQueueFFT, CL_QUEUE_CONTEXT, sizeof(cl_context), &QueueContext, NULL);
    OPENCL_V(status, _T("clGetCommandQueueInfo failed"));


    OPENCL_V(fftRepo.setProgramCode(Transpose_NONSQUARE, this->getSignatureData(), programCode, Device, QueueContext), _T("fftRepo.setclString() failed!"));
    if (this->signature.nonSquareKernelType == NON_SQUARE_TRANS_TRANSPOSE)
    {
        // Note:  See genFunctionPrototype( )
        if (this->signature.fft_3StepTwiddle)
        {
            OPENCL_V(fftRepo.setProgramEntryPoints(Transpose_NONSQUARE, this->getSignatureData(), "transpose_nonsquare_tw_fwd", "transpose_nonsquare_tw_back", Device, QueueContext), _T("fftRepo.setProgramEntryPoint() failed!"));
        }
        else
        {
            OPENCL_V(fftRepo.setProgramEntryPoints(Transpose_NONSQUARE, this->getSignatureData(), "transpose_nonsquare", "transpose_nonsquare", Device, QueueContext), _T("fftRepo.setProgramEntryPoint() failed!"));
        }
    }
    else
    {
        OPENCL_V(fftRepo.setProgramEntryPoints(Transpose_NONSQUARE, this->getSignatureData(), "swap_nonsquare", "swap_nonsquare", Device, QueueContext), _T("fftRepo.setProgramEntryPoint() failed!"));
    }
    return CLFFT_SUCCESS;
}


clfftStatus FFTGeneratedTransposeNonSquareAction::getWorkSizes(std::vector< size_t >& globalWS, std::vector< size_t >& localWS)
{

    size_t wg_slice;
    size_t smaller_dim = (this->signature.fft_N[0] < this->signature.fft_N[1]) ? this->signature.fft_N[0] : this->signature.fft_N[1];
    size_t global_item_size;

    if (this->signature.nonSquareKernelType == NON_SQUARE_TRANS_TRANSPOSE)
    {
        if (smaller_dim % (16 * reShapeFactor) == 0)
            wg_slice = smaller_dim / 16 / reShapeFactor;
        else
            wg_slice = (smaller_dim / (16 * reShapeFactor)) + 1;

        global_item_size = wg_slice*(wg_slice + 1) / 2 * 16 * 16 * this->plan->batchsize;

        for (int i = 2; i < this->signature.fft_DataDim - 1; i++)
        {
            global_item_size *= this->signature.fft_N[i];
        }

        /*Push the data required for the transpose kernels*/
        globalWS.clear();
        globalWS.push_back(global_item_size * 2);

        localWS.clear();
        localWS.push_back(lwSize);
    }
    else
    {
        /*Now calculate the data for the swap kernels */

        size_t input_elm_size_in_bytes;
        switch (this->signature.fft_precision)
        {
        case CLFFT_SINGLE:
        case CLFFT_SINGLE_FAST:
            input_elm_size_in_bytes = 4;
            break;
        case CLFFT_DOUBLE:
        case CLFFT_DOUBLE_FAST:
            input_elm_size_in_bytes = 8;
            break;
        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }

        switch (this->signature.fft_outputLayout)
        {
        case CLFFT_COMPLEX_INTERLEAVED:
        case CLFFT_COMPLEX_PLANAR:
            input_elm_size_in_bytes *= 2;
            break;
        case CLFFT_REAL:
            break;
        default:
            return CLFFT_TRANSPOSED_NOTIMPLEMENTED;
        }
        size_t max_elements_loaded = AVAIL_MEM_SIZE / input_elm_size_in_bytes;
        size_t num_elements_loaded;
        size_t local_work_size_swap, num_grps_pro_row;

        if ((max_elements_loaded >> 1) > smaller_dim)
        {
            local_work_size_swap = (smaller_dim < 256) ? smaller_dim : 256;
            num_elements_loaded = smaller_dim;
            num_grps_pro_row = 1;
        }
        else
        {
            num_grps_pro_row = (smaller_dim << 1) / max_elements_loaded;
            num_elements_loaded = max_elements_loaded >> 1;
            local_work_size_swap = (num_elements_loaded < 256) ? num_elements_loaded : 256;
        }
        size_t num_reduced_row;
        size_t num_reduced_col;

        if (this->signature.fft_N[1] == smaller_dim)
        {
            num_reduced_row = smaller_dim;
            num_reduced_col = 2;
        }
        else
        {
            num_reduced_row = 2;
            num_reduced_col = smaller_dim;
        }

        size_t *cycle_map = new size_t[num_reduced_row * num_reduced_col * 2];
        /* The memory required by cycle_map cannot exceed 2 times row*col by design*/
        get_cycles(cycle_map, num_reduced_row, num_reduced_col);

        global_item_size = local_work_size_swap * num_grps_pro_row * cycle_map[0] * this->plan->batchsize;

        for (int i = 2; i < this->signature.fft_DataDim - 1; i++)
        {
            global_item_size *= this->signature.fft_N[i];
        }
        delete[] cycle_map;

        globalWS.push_back(global_item_size);
        localWS.push_back(local_work_size_swap);
    }
    return CLFFT_SUCCESS;
}
