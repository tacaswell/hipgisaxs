/**
 *  Project: HipGISAXS (High-Performance GISAXS)
 *
 *  File: object2hdf5.c
 *  Created: Aug 25, 2012
 *  Modified: Tue 16 Jul 2013 12:15:41 PM PDT
 *
 *  Author: Abhinav Sarje <asarje@lbl.gov>
 *  Developers: Slim Chourou <stchourou@lbl.gov>
 *              Abhinav Sarje <asarje@lbl.gov>
 *              Elaine Chan <erchan@lbl.gov>
 *              Alexander Hexemer <ahexemer@lbl.gov>
 *              Xiaoye Li <xsli@lbl.gov>
 *
 *  Licensing: The HipGISAXS software is only available to be downloaded and
 *  used by employees of academic research institutions, not-for-profit
 *  research laboratories, or governmental research facilities. Please read the
 *  accompanying LICENSE file before downloading the software. By downloading
 *  the software, you are agreeing to be bound by the terms of this
 *  NON-COMMERCIAL END USER LICENSE AGREEMENT.
 */

#include <string.h>

#include "object2hdf5.h"

/**
 * converts shape def to HDF5 format and stores into a hdf5 file
 */
void s2h_converter(double** shape_def, unsigned int num_triangles, char* hdf5_filename, MPI_Comm comm) {
	hid_t prop_id, file_id;

	// set up parallel i/o access
	prop_id = H5Pcreate(H5P_FILE_ACCESS);
	H5Pset_fapl_mpio(prop_id, comm, MPI_INFO_NULL);

	file_id = H5Fcreate(hdf5_filename, H5F_ACC_TRUNC, H5P_DEFAULT, prop_id);

	hid_t dataset, datatype, dataspace;
	int num_dims = 2;
	hsize_t dims[num_dims];						// currently treating the whole thing as one
												// later may be change to each triangle line separate
	dims[0] = num_triangles; dims[1] = 7;

//	double** temp_shape = (double **) malloc(num_triangles * sizeof(double *));
	//double temp_shape[num_triangles][7];
	int i, j;
/*	double* temp_shape = (double*) malloc(num_triangles * 8 * sizeof(double));
	for(i = 0; i < num_triangles; ++ i) {
//		temp_shape[i] = (*shape_def) + i * 7;
		for(j = 0; j < 7; ++ j)
	//		temp_shape[i][j] = (*shape_def)[7 * i + j];
			temp_shape[7 * i + j] = (*shape_def)[7 * i + j];
	} // for
*/
	/*for(i = 0; i < num_triangles; ++ i) {
		for(j = 0; j < 7; ++ j)
			printf("%f\t", (*shape_def)[7 * i + j]);
		printf("\n");
	} // for */

	dataspace = H5Screate_simple(num_dims, dims, NULL);
	datatype = H5Tcopy(H5T_NATIVE_DOUBLE);
	herr_t status = H5Tset_order(datatype, H5T_ORDER_LE);
	dataset = H5Dcreate1(file_id, "shape_def", datatype, dataspace, H5P_DEFAULT);

	//hid_t coll_prop_id = H5Pcreate(H5P_DATASET_XFER);
	//H5Pset_dxpl_mpio(coll_prop_id, H5FD_MPIO_COLLECTIVE);

	status = H5Dwrite(dataset, datatype, H5S_ALL, H5S_ALL, H5P_DEFAULT, (*shape_def));
	//status = H5Dwrite(dataset, datatype, H5S_ALL, H5S_ALL, coll_prop_id, temp_shape);

//	free(temp_shape);

	H5Sclose(dataspace);
	H5Tclose(datatype);
	H5Dclose(dataset);
	H5Pclose(prop_id);
	H5Fclose(file_id);
} // s2h_converter()


/**
 * reads a hdf5 file and converts to shape def format
 */
void h5_shape_reader(const char* hdf5_filename, double** shape_def, unsigned int* num_triangles/*, MPI_Comm comm*/) {
	hid_t file_id, dataset, dataspace, datatype, class, order;

	file_id = H5Fopen(hdf5_filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	dataset = H5Dopen(file_id, "shape_def", H5P_DEFAULT);
	dataspace = H5Dget_space(dataset);
	datatype = H5Dget_type(dataset);
	class = H5Tget_class(datatype);
	order = H5Tget_order(datatype);
	int size = H5Tget_size(datatype);

	// check for datatype size ...

	int num_dim = H5Sget_simple_extent_ndims(dataspace);
	hsize_t* dims = (hsize_t*) malloc(sizeof(hsize_t) * num_dim);
	hsize_t* max_dims = (hsize_t*) malloc(sizeof(hsize_t) * num_dim);
	herr_t status = H5Sget_simple_extent_dims(dataspace, dims, max_dims);

	//printf("DATATYPE = %d, CLASS = %d, SIZE = %d\n", datatype, class, size);
	//printf("Dimensions = %d: %d x %d \n", num_dim, dims[0], dims[1]);

	(*num_triangles) = dims[0];

	(*shape_def) = (double *) malloc(dims[0] * dims[1] * size);
	if((*shape_def) == NULL) {
		fprintf(stderr, "error: cannot allocate memory to read shape definition data\n");
		*num_triangles = 0;
	} else {
		memset(&(**shape_def), 0, dims[0] * dims[1] * size);
		status = H5Dread(dataset, datatype, H5S_ALL, H5S_ALL, H5P_DEFAULT, *shape_def);
		if(status < 0) {
			fprintf(stderr, "error: shape definition data reading failed\n");
			*num_triangles = 0;
		} // if
	} // if-else

	free(max_dims);
	free(dims);
	H5Sclose(dataspace);
	H5Tclose(datatype);
	H5Dclose(dataset);
	H5Fclose(file_id);
} // h5_shape_reader()