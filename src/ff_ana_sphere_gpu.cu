/***
  *  $Id: ff_ana_gpu.cu 37 2012-08-09 22:59:59Z asarje $
  *
  *  Project: HipGISAXS (High-Performance GISAXS)
  *
  *  File: ff_ana_gpu.cu
  *  Created: Oct 16, 2012
  *  Modified: Tue 19 Feb 2013 11:05:09 AM PST
  *
  *  Author: Abhinav Sarje <asarje@lbl.gov>
  */

#include <iostream>
#include <complex>
#include <cuComplex.h>

#include "ff_ana_gpu.cuh"
#include "enums.hpp"
#include "cu_complex_numeric.cuh"

namespace hig {

	// make gpu kernels members of the class ...

	// forward declarations of gpu kernels
	__global__ void form_factor_sphere_kernel(unsigned int nqx, unsigned int nqy, unsigned int nqz,
					//cucomplex_t* mesh_qx, cucomplex_t* mesh_qy, cucomplex_t* mesh_qz,
					float_t* qx, float_t* qy, cucomplex_t* mesh_qz, float_t* rot,
					unsigned int n_r, float_t* r, unsigned int n_distr_r, float_t* distr_r,
					unsigned int n_transvec, float_t* transvec, cucomplex_t* ff);
	__device__ void ff_sphere_kernel_compute_tff(float r, float distr_r,
										cuFloatComplex q, cuFloatComplex mqz, cuFloatComplex& f);
	__device__ void ff_sphere_kernel_compute_tff(double r, double distr_r,
										cuDoubleComplex q, cuDoubleComplex mqz, cuDoubleComplex& f);
	__device__ cuFloatComplex ff_sphere_kernel_compute_ff(cuFloatComplex temp_f,
								cuFloatComplex mqx, cuFloatComplex mqy, cuFloatComplex mqz,
								float tx, float ty, float tz);
	__device__ cuDoubleComplex ff_sphere_kernel_compute_ff(cuDoubleComplex temp_f,
								cuDoubleComplex mqx, cuDoubleComplex mqy, cuDoubleComplex mqz,
								double tx, double ty, double tz);


	/**
	 * sphere host function
	 */
	bool AnalyticFormFactorG::compute_sphere(const std::vector<float_t>& r,
											const std::vector<float_t>& distr_r,
											const float_t* qx_h,
											const float_t* qy_h,
											const cucomplex_t* qz_h,
											const float_t* rot_h,
											const std::vector<float_t>& transvec,
											std::vector<complex_t>& ff) {
		unsigned int n_r = r.size();
		unsigned int n_distr_r = distr_r.size();
		unsigned int n_transvec = transvec.size();		// this should be = 3
		const float_t *r_h = r.empty() ? NULL : &*r.begin();
		const float_t *distr_r_h = distr_r.empty() ? NULL : &*distr_r.begin();
		const float_t *transvec_h = transvec.empty() ? NULL : &*transvec.begin();

		unsigned int grid_size = nqx_ * nqy_ * nqz_;
		//std::cout << "nqx x nqy x nqz = " << nqx_ << " x " << nqy_ << " x " << nqz_ << std::endl;

		cudaEvent_t mem_begin_e, mem_end_e;
		cudaEventCreate(&mem_begin_e);
		cudaEventCreate(&mem_end_e);
		float mem_time = 0.0, temp_time = 0.0;

		cudaEventRecord(mem_begin_e, 0);

		// construct device buffers
		float_t *qx_d, *qy_d; cucomplex_t *qz_d, *ff_d;
		float_t *r_d, *distr_r_d, *transvec_d;
		float_t *rot_d;
		if(cudaMalloc((void **) &qx_d, nqx_ * sizeof(float_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for mesh_qx_d" << std::endl;
			return false;
		} // if
		if(cudaMalloc((void **) &qy_d, nqy_ * sizeof(float_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for mesh_qy_d" << std::endl;
			cudaFree(qx_d);
			return false;
		} // if
		if(cudaMalloc((void **) &qz_d, nqz_ * sizeof(cucomplex_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for mesh_qz_d" << std::endl;
			cudaFree(qy_d);
			cudaFree(qx_d);
			return false;
		} // if
		if(cudaMalloc((void **) &ff_d, grid_size * sizeof(cucomplex_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for mesh_qz_d" << std::endl;
			cudaFree(qz_d);
			cudaFree(qy_d);
			cudaFree(qx_d);
			return false;
		} // if
		if(cudaMalloc((void **) &r_d, n_r * sizeof(float_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for r_d" << std::endl;
			cudaFree(ff_d);
			cudaFree(qz_d);
			cudaFree(qy_d);
			cudaFree(qx_d);
			return false;
		} // if
		if(cudaMalloc((void **) &distr_r_d, n_distr_r * sizeof(float_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for n_distr_r_d" << std::endl;
			cudaFree(r_d);
			cudaFree(ff_d);
			cudaFree(qz_d);
			cudaFree(qy_d);
			cudaFree(qx_d);
			return false;
		} // if
		if(cudaMalloc((void **) &transvec_d, n_transvec * sizeof(float_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for transvec_d" << std::endl;
			cudaFree(distr_r_d);
			cudaFree(r_d);
			cudaFree(ff_d);
			cudaFree(qz_d);
			cudaFree(qy_d);
			cudaFree(qx_d);
			return false;
		} // if
		if(cudaMalloc((void **) &rot_d, 9 * sizeof(float_t)) != cudaSuccess) {
			std::cerr << "error: device memory allocation failed for rot_d" << std::endl;
			cudaFree(transvec_d);
			cudaFree(distr_r_d);
			cudaFree(r_d);
			cudaFree(ff_d);
			cudaFree(qz_d);
			cudaFree(qy_d);
			cudaFree(qx_d);
			return false;
		} // if

		// copy data to device buffers
		cudaMemcpy(qx_d, qx_h, nqx_ * sizeof(float_t), cudaMemcpyHostToDevice);
		cudaMemcpy(qy_d, qy_h, nqy_ * sizeof(float_t), cudaMemcpyHostToDevice);
		cudaMemcpy(qz_d, qz_h, nqz_ * sizeof(cucomplex_t), cudaMemcpyHostToDevice);
		cudaMemcpy(r_d, r_h, n_r * sizeof(float_t), cudaMemcpyHostToDevice);
		cudaMemcpy(distr_r_d, distr_r_h, n_distr_r * sizeof(float_t), cudaMemcpyHostToDevice);
		cudaMemcpy(transvec_d, transvec_h, n_transvec * sizeof(float_t), cudaMemcpyHostToDevice);
		cudaMemcpy(rot_d, rot_h, 9 * sizeof(float_t), cudaMemcpyHostToDevice);

		cudaEventRecord(mem_end_e, 0);
		cudaEventSynchronize(mem_end_e);
		cudaEventElapsedTime(&temp_time, mem_begin_e, mem_end_e);
		mem_time += temp_time;

		size_t device_mem_avail, device_mem_total, device_mem_used;
		cudaMemGetInfo(&device_mem_avail, &device_mem_total);
		device_mem_used = device_mem_total - device_mem_avail;
//		if(rank == 0) {
			std::cout << "++       Used device memory: " << (float) device_mem_used / 1024 / 1024
						<< " MB" << std::endl;
			std::cout << "++       Free device memory: " << (float) device_mem_avail / 1024 / 1024
						<< " MB" << std::endl;
//		}

		//for(int cby = 2; cby < 129; cby += 2) {
		//for(int cbz = 2; cbz < 129; cbz += 2) {
		cudaEvent_t begin_e, end_e;
		cudaEventCreate(&begin_e);
		cudaEventCreate(&end_e);
		cudaEventRecord(begin_e, 0);
		// decompose computations and construct and call the kernel
		// decomposing along y and z
		// note that (cuda x y z != dim x y z)
		unsigned int cuda_block_y = 16, cuda_block_z = 6;
		unsigned int cuda_num_blocks_y = (unsigned int) ceil((float_t) nqy_ / cuda_block_y);
		unsigned int cuda_num_blocks_z = (unsigned int) ceil((float_t) nqz_ / cuda_block_z);
		dim3 ff_grid_size(cuda_num_blocks_y, cuda_num_blocks_z, 1);
		dim3 ff_block_size(cuda_block_y, cuda_block_z, 1);

		size_t shared_mem_size = (nqx_ + cuda_block_y) * sizeof(float_t) +
									cuda_block_z * sizeof(cucomplex_t);
		if(shared_mem_size > 49152) {
			std::cerr << "Too much shared memory requested!" << std::endl;
			return false;
		} // if

		// the kernel
		form_factor_sphere_kernel <<< ff_grid_size, ff_block_size, shared_mem_size >>> (
				nqx_, nqy_, nqz_, qx_d, qy_d, qz_d, rot_d,
				n_r, r_d, n_distr_r, distr_r_d, n_transvec, transvec_d,
				ff_d);

		cudaThreadSynchronize();
		cudaError_t err = cudaGetLastError();
		if(err != cudaSuccess) {
			std::cerr << "error: form factor kernel failed [" << __FILE__ << ":" << __LINE__ << "]: "
						<< cudaGetErrorString(err) << std::endl;
		} else {
			float kernel_time;
			cudaEventRecord(end_e, 0);
			cudaEventSynchronize(end_e);
			cudaEventElapsedTime(&kernel_time, begin_e, end_e);
			//std::cout << "block size: " << cby << " x " << cbz << ". ";
			std::cout << "Analytical Sphere Kernel completed in " << kernel_time << " ms." << std::endl;

			cudaEventRecord(mem_begin_e, 0);

			cucomplex_t* ff_h = new (std::nothrow) cucomplex_t[nqx_ * nqy_ * nqz_];
			// copy result to host
			cudaMemcpy(ff_h, ff_d, nqx_ * nqy_ * nqz_ * sizeof(cucomplex_t), cudaMemcpyDeviceToHost);
			for(unsigned int i = 0; i < nqx_ * nqy_ * nqz_; ++ i) {
				ff.push_back(complex_t(ff_h[i].x, ff_h[i].y));
			} // for
			delete[] ff_h;

			cudaEventRecord(mem_end_e, 0);
			cudaEventSynchronize(mem_end_e);
			cudaEventElapsedTime(&temp_time, mem_begin_e, mem_end_e);
			mem_time += temp_time;
		} // if-else

		std::cout << "GPU memory time: " << mem_time << " ms." << std::endl;
		//} // for cbz
		//} // for cby

		cudaFree(rot_d);
		cudaFree(transvec_d);
		cudaFree(distr_r_d);
		cudaFree(r_d);
		cudaFree(ff_d);
		cudaFree(qz_d);
		cudaFree(qy_d);
		cudaFree(qx_d);

		return true;
	} // AnalyticFormFactorG::compute_sphere()


	// sphere gpu kernel
/*	__global__ void form_factor_sphere_kernel(unsigned int nqx, unsigned int nqy, unsigned int nqz,
					float_t* qx, float_t* qy, cucomplex_t* qz, float_t* rot,
					unsigned int n_r, float_t* r, unsigned int n_distr_r, float_t* distr_r,
					unsigned int n_transvec, float_t* transvec, cucomplex_t* ff) {
		// decomposition is along y and z
		unsigned int i_y = blockDim.x * blockIdx.x + threadIdx.x;
		unsigned int i_z = blockDim.y * blockIdx.y + threadIdx.y;
		unsigned int base_index = nqx * nqy * i_z + nqx * i_y;
		// compute
		if(i_y < nqy && i_z < nqz) {
			for(unsigned int i_x = 0; i_x < nqx; ++ i_x) {
				unsigned int index = base_index + i_x;
				// computing mesh values on the fly instead of storing them
				cucomplex_t temp_mqx = make_cuC(qy[i_y] * rot[0] + qx[i_x] * rot[1] + qz[i_z].x * rot[2],
												qz[i_z].y * rot[2]);
				cucomplex_t temp_mqy = make_cuC(qy[i_y] * rot[3] + qx[i_x] * rot[4] + qz[i_z].x * rot[5],
												qz[i_z].y * rot[5]);
				cucomplex_t temp_mqz = make_cuC(qy[i_y] * rot[6] + qx[i_x] * rot[7] + qz[i_z].x * rot[8],
												qz[i_z].y * rot[8]);
				cucomplex_t q = cuCnorm3(temp_mqx, temp_mqy, temp_mqz);
				cucomplex_t temp_f = make_cuC((float_t)0.0, (float_t)0.0);
				for(unsigned int i_r = 0; i_r < n_r; ++ i_r) {
					float_t temp_r = r[i_r];
					ff_sphere_kernel_compute_tff(temp_r, distr_r[i_r], q, temp_mqz, temp_f);

				} // for i_r
				ff[index] = ff_sphere_kernel_compute_ff(temp_f,	temp_mqx, temp_mqy, temp_mqz,
													transvec[0], transvec[1], transvec[2]);
			} // for x
		} // if
	} // form_factor_sphere_kernel() 
*/
	extern __shared__ float_t dynamic_shared[];

	// sphere gpu kernel
	__global__ void form_factor_sphere_kernel(unsigned int nqx, unsigned int nqy, unsigned int nqz,
					float_t* qx, float_t* qy, cucomplex_t* qz, float_t* rot,
					unsigned int n_r, float_t* r, unsigned int n_distr_r, float_t* distr_r,
					unsigned int n_transvec, float_t* transvec, cucomplex_t* ff) {
		// decomposition is along y and z
		unsigned int i_y = blockDim.x * blockIdx.x + threadIdx.x;
		unsigned int i_z = blockDim.y * blockIdx.y + threadIdx.y;
		unsigned int base_index = nqx * nqy * i_z + nqx * i_y;

		// shared buffers
		//float_t* qx_s = (float_t*) dynamic_shared;
		//float_t* qy_s = (float_t*) &qx_s[nqx];
		//cucomplex_t* qz_s = (cucomplex_t*) &qy_s[blockDim.x];
		cucomplex_t* qz_s = (cucomplex_t*) dynamic_shared;
		float_t* qx_s = (float_t*) &qz_s[blockDim.y];
		float_t* qy_s = (float_t*) &qx_s[nqx];

		// load all qx
		unsigned int i_thread = blockDim.x * threadIdx.y + threadIdx.x;
		unsigned int num_threads = blockDim.x * blockDim.y;
		unsigned int num_loads = ceil((float_t) nqx / num_threads);
		for(int i = 0; i < num_loads; ++ i) {
			unsigned int index = i * num_threads + i_thread;
			if(index < nqx) qx_s[index] = qx[index];
			else ;	// nop
		} // for
		// load part of qy
		if(i_y < nqy && threadIdx.y == 0)	// first row of threads
			qy_s[threadIdx.x] = qy[i_y];
		// load part of qz
		if(i_z < nqz && threadIdx.x == 0)	// first column of threads
			qz_s[threadIdx.y] = qz[i_z];

		__syncthreads();

		// compute
		if(i_y < nqy && i_z < nqz) {
			for(unsigned int i_x = 0; i_x < nqx; ++ i_x) {
				// computing mesh values on the fly instead of storing them
				cucomplex_t temp_mqx = make_cuC(qy_s[threadIdx.x] * rot[0] + qx_s[i_x] * rot[1] +
												qz_s[threadIdx.y].x * rot[2],
												qz_s[threadIdx.y].y * rot[2]);
				cucomplex_t temp_mqy = make_cuC(qy_s[threadIdx.x] * rot[3] + qx_s[i_x] * rot[4] +
												qz_s[threadIdx.y].x * rot[5],
												qz_s[threadIdx.y].y * rot[5]);
				cucomplex_t temp_mqz = make_cuC(qy_s[threadIdx.x] * rot[6] + qx_s[i_x] * rot[7] +
												qz_s[threadIdx.y].x * rot[8],
												qz_s[threadIdx.y].y * rot[8]);
				cucomplex_t q = cuCnorm3(temp_mqx, temp_mqy, temp_mqz);
				cucomplex_t temp_f = make_cuC((float_t)0.0, (float_t)0.0);
				for(unsigned int i_r = 0; i_r < n_r; ++ i_r) {
					float_t temp_r = r[i_r];
					float_t temp_distr_r = distr_r[i_r];
					ff_sphere_kernel_compute_tff(temp_r, temp_distr_r, q, temp_mqz, temp_f);
				} // for i_r
				ff[base_index + i_x] = ff_sphere_kernel_compute_ff(temp_f,	temp_mqx, temp_mqy, temp_mqz,
													transvec[0], transvec[1], transvec[2]);
			} // for x
		} // if
	} // form_factor_sphere_kernel()


	__device__ void ff_sphere_kernel_compute_tff(float r, float distr_r,
										cuFloatComplex q, cuFloatComplex mqz, cuFloatComplex &f) {
		cuFloatComplex temp1 = make_cuFloatComplex(q.x * r, q.y * r);
		cuFloatComplex temp2 = cuCsubf(cuCsin(temp1), cuCmulf(temp1, cuCcos(temp1)));
		cuFloatComplex temp3 = cuCmulf(temp1, cuCmulf(temp1, temp1));
		float temp4 = distr_r * 4 * PI_ * r * r * r;
		cuFloatComplex temp5 = cuCmulf(cuCdivf(temp2, temp3),
									cuCexp(make_cuFloatComplex(-r * mqz.y, r * mqz.x)));
		cuFloatComplex tempff = make_cuFloatComplex(temp4 * temp5.x, temp4 * temp5.y);
		f = cuCaddf(f, tempff);
	} // ff_sphere_kernel_compute_tff()


	__device__ void ff_sphere_kernel_compute_tff(double r, double distr_r,
										cuDoubleComplex q, cuDoubleComplex mqz, cuDoubleComplex &f) {
		cuDoubleComplex temp1 = cuCmul(q, make_cuDoubleComplex(r, 0.0));
		cuDoubleComplex temp2 = cuCsub(cuCsin(temp1), cuCmul(temp1, cuCcos(temp1)));
		cuDoubleComplex temp3 = cuCmul(temp1, cuCmul(temp1, temp1));
		double temp4 = distr_r * 4 * PI_ * r * r * r;
		cuDoubleComplex temp5 = cuCmul(cuCdiv(temp2, temp3),
									cuCexp(make_cuDoubleComplex(-r * mqz.y, r * mqz.x)));
		cuDoubleComplex tempff = make_cuDoubleComplex(temp4 * temp5.x, temp4 * temp5.y);
		f = cuCadd(f, tempff);
	} // ff_sphere_kernel_compute_tff()


	__device__ cuFloatComplex ff_sphere_kernel_compute_ff(cuFloatComplex temp_f,
								cuFloatComplex mqx, cuFloatComplex mqy, cuFloatComplex mqz,
								float tx, float ty, float tz) {
			float rl = tx * mqx.x + ty * mqy.x + tz * mqz.x;
			float im = tx * mqx.y + ty * mqy.y + tz * mqz.y;
			cuFloatComplex temp1 = cuCexp(make_cuFloatComplex(-im, rl));
			return cuCmulf(temp_f, temp1);
	} // ff_sphere_kernel_compute_ff()


	__device__ cuDoubleComplex ff_sphere_kernel_compute_ff(cuDoubleComplex temp_f,
								cuDoubleComplex mqx, cuDoubleComplex mqy, cuDoubleComplex mqz,
								double tx, double ty, double tz) {
			double rl = tx * mqx.x + ty * mqy.x + tz * mqz.x;
			double im = tx * mqx.y + ty * mqy.y + tz * mqz.y;
			cuDoubleComplex temp1 = cuCexp(make_cuDoubleComplex(-im, rl));
			return cuCmul(temp_f, temp1);
	} // ff_sphere_kernel_compute_ff()


} // namespace hig
