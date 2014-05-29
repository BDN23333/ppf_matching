
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
/*#include "opencv2/features2d.hpp"
#include "opencv2/nonfree.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/core.hpp"
#include "opencv2/core/utility.hpp"
#include "opencv2/imgproc.hpp"*/
#include "opencv2/core.hpp"
#include "opencv2/features2d.hpp"
#include "opencv2/rgbd.hpp"
#include "helpers.h"
#include "c_utils.h"
#include "hash_murmur.h"
#include <tommy.h>

#include <Eigen/Core>
#include "opencv2/core/eigen.hpp"


using namespace cv;

typedef struct THash {
	int id;
	void* data;
	tommy_node node;
} THash;

typedef struct
{
	int magic;
	double maxDist, angleStep, distStep;
	Mat inputPC;
	flann::Index pcTree;
	Mat alpha_m;
	int n;
	THash* hashTable;
}TPPFModelPC;


#define T_PPF_LENGTH 5

// compute per point PPF as in paper
void compute_ppf_features(const double p1[4], const double n1[4],
						  const double p2[4], const double n2[4],
						  double f[4])
{
	double d[4] = {p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2], 0};

	f[3] = sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);

	if (f[3])
	{
		d[0] /= f[3];
		d[1] /= f[3];
		d[2] /= f[3];
	}
	else
	{
		// TODO: Handle this
		f[0] = 0;
		f[1] = 0;
		f[2] = 0;
		return ;
	}

	// dot products 
	f[0] = n1[0] * d[0] + n1[1] * d[1] + n1[2] * d[2];
	f[1] = n2[0] * d[0] + n2[1] * d[1] + n2[2] * d[2];
	f[2] = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2];

	// I have not computed the angles here as I don't want to compute acos yet.
}

// simple hashing
int hash_ppf_simple(const double f[4], const double AngleStep, const double DistanceStep)
{
	const int d1 = (int) (floor ((double)f[0] / (double)AngleStep));
	const int d2 = (int) (floor ((double)f[1] / (double)AngleStep));
	const int d3 = (int) (floor ((double)f[2] / (double)AngleStep));
	const int d4 = (int) (floor ((double)f[3] / (double)DistanceStep));
	
	return (d1 & (d2<<8) & (d3<<16) & (d4<<24));
}

// quantize ppf and hash it for proper indexing
int hash_ppf(const double f[4], const double AngleStep, const double DistanceStep)
{
	const int d1 = (int) (floor ((double)f[0] / (double)AngleStep));
	const int d2 = (int) (floor ((double)f[1] / (double)AngleStep));
	const int d3 = (int) (floor ((double)f[2] / (double)AngleStep));
	const int d4 = (int) (floor ((double)f[3] / (double)DistanceStep));
	int key[4]={d1,d2,d3,d4};
	int hashKey=0;
	MurmurHash3_x86_32(key, 4, 42, &hashKey);
	return hashKey;
}

int compare(const void* arg, const void* obj)
{
	return *(const int*)arg != ((THash*)obj)->id;
}

// TODO: An initial attempt. I will double check this
double compute_alpha(const double p1[4], const double n1[4], const double p2[4])
{
	double Tmg[3], mpt[3], row2[3], row3[3], alpha;

	compute_transform_rt_yz(p1, n1, row2, row3, Tmg);

	mpt[1] = Tmg[1] + row2[0] * p2[0] + row2[1] * p2[1] + row2[2] * p2[2];
    mpt[2] = Tmg[2] + row3[0] * p2[0] + row3[1] * p2[1] + row3[2] * p2[2];

	alpha=atan2(-mpt[2], mpt[1]);

	if ( alpha != alpha)
    {
		printf("NaN value!\n");
		return 0;
	}

	if (sin(alpha)*mpt[2]<0.0)
		alpha=-alpha;

	return (-alpha);
}


Mat compute_ppf_pc_train(const Mat PC, const double distanceStep, const double angleStep)
{
	Mat PPFMat = Mat(PC.rows*PC.rows, T_PPF_LENGTH, CV_32FC1);

	for (int i=0; i<PC.rows; i++)
	{
		for (int j=0; j<PC.rows; j++)
		{
			// cannnot compute the ppf with myself
			if (i!=j)
			{
				float* f1 = (float*)(&PC.data[i * PC.step]);
				float* f2 = (float*)(&PC.data[j * PC.step]);
				const double p1[4] = {f1[0], f1[1], f1[2], 1};
				const double p2[4] = {f2[0], f1[1], f1[2], 1};
				const double n1[4] = {f1[3], f1[4], f1[5], 1};
				const double n2[4] = {f2[3], f1[4], f1[5], 1};

				double f[4]={0};
				compute_ppf_features(p1, n1, p2, n2, f);
				double alpha = compute_alpha(p1, n1, p2);

				int corrInd = i*PC.rows+j;
				PPFMat.data[ corrInd ] = f[0];
				PPFMat.data[ corrInd + 1 ] = f[1];
				PPFMat.data[ corrInd + 2 ] = f[2];
				PPFMat.data[ corrInd + 3 ] = f[3];
				PPFMat.data[ corrInd + 4 ] = (float)alpha;
			}
		}
	}

	return PPFMat;
}

// TODO: Check all step sizes to be positive
Mat train_pc_ppf(const Mat PC, const double sampling_step_relative, const double distance_step_relative, const double angle_step_relative, TPPFModelPC** Model3D)
{
	const int numPoints = PC.rows;

	// compute bbox
	float xRange[2], yRange[2], zRange[2];
	compute_obb(PC, xRange, yRange, zRange);

	// compute sampling step from diameter of bbox
	float dx = xRange[1] - xRange[0];
	float dy = yRange[1] - yRange[0];
	float dz = zRange[1] - zRange[0];
	float diameter = sqrt ( dx * dx + dy * dy + dz * dz );
	float distanceStep = diameter * sampling_step_relative;

	float angleStepRadians = PI * angle_step_relative / 180.0;

	tommy_hashtable* hashTable = (tommy_hashtable*)malloc(sizeof(tommy_hashtable));
	Mat PPFMat = Mat(PC.rows*PC.rows, T_PPF_LENGTH, CV_32FC1);

	// 262144 = 2^18
	int size = next_power_of_two(100000);
	tommy_hashtable_init(hashTable, size);

	for (int i=0; i<PC.rows; i++)
	{
		for (int j=0; j<PC.rows; j++)
		{
			// cannnot compute the ppf with myself
			if (i!=j)
			{
				float* f1 = (float*)(&PC.data[i * PC.step]);
				float* f2 = (float*)(&PC.data[j * PC.step]);
				const double p1[4] = {f1[0], f1[1], f1[2], 1};
				const double p2[4] = {f2[0], f1[1], f1[2], 1};
				const double n1[4] = {f1[3], f1[4], f1[5], 1};
				const double n2[4] = {f2[3], f1[4], f1[5], 1};

				double f[4]={0};
				compute_ppf_features(p1, n1, p2, n2, f);
				int hash = hash_ppf_simple(f, angleStepRadians, distanceStep);
				double alpha = compute_alpha(p1, n1, p2);
				int corrInd = i*PC.rows+j;

				THash* hashNode = (THash*)calloc(1, sizeof(THash));
				hashNode->id = hash;
				hashNode->data = (void*)corrInd;
				tommy_hashtable_insert(hashTable, &hashNode->node, hashNode, tommy_inthash_u32(hashNode->id));

				PPFMat.data[ corrInd ] = f[0];
				PPFMat.data[ corrInd + 1 ] = f[1];
				PPFMat.data[ corrInd + 2 ] = f[2];
				PPFMat.data[ corrInd + 3 ] = f[3];
				PPFMat.data[ corrInd + 4 ] = (float)alpha;
			}
		}
	}

	//return compute_ppf_pc_train(pc, distanceStep, angleStepRadians);


	
	return PPFMat;
}

int main()
{
	int useNormals = 1;
	int withBbox = 1;
	int numVert = 176920;
	const char* fn = "../../../data/cheff2.ply";
	Mat pc = load_ply_simple(fn, numVert, useNormals);

	TPPFModelPC* ppfModel = 0;
	Mat PPFMAt = train_pc_ppf(pc, 0.05, 0.05, 30, &ppfModel);


	//compute_ppf_pc(pc, PPFMAt, const double RelSamplingStep, const double RelativeAngleStep, const double RelativeDistanceStep, TPPFModelPC** Model3D)

	return 0;
}

// test bounding box  _bbox
int main_bbox()
{
	int useNormals = 1;
	int withBbox = 1;
	int numVert = 176920;
	const char* fn = "../../../data/cheff2.ply";
	Mat pc = load_ply_simple(fn, numVert, useNormals);

	float xRange[2], yRange[2], zRange[2];
	compute_obb(pc, xRange, yRange, zRange);

	printf("Bounding box -- x: (%f, %f), y: (%f, %f), z: (%f, %f)\n", xRange[0], xRange[1], yRange[0], yRange[1], zRange[0], zRange[1]);

	visualize_pc(pc, useNormals, withBbox, "Point Cloud");

	return 0;
}

// test point cloud reading & visualization
// for now uniform sampling looks better
int main_ply()
{
	int useNormals = 1;
	int withBbox = 0;
	int numVert = 6700;
	const char* fn = "../../../data/parasaurolophus_6700_2.ply";

	Mat pc = load_ply_simple(fn, numVert, useNormals);
	
	Mat spc1 = sample_pc_uniform(pc, numVert/350);
	Mat spc2 = sample_pc_random(pc, 350);
	visualize_pc(pc, useNormals, withBbox, "PC1");
	visualize_pc(spc1, useNormals, withBbox, "Uniform Sampled");
	visualize_pc(spc2, useNormals, withBbox, "Random Sampled");

	return 0;
}
    
// test hash table
int main_hash_test()
{
	int value_to_find = 227;
	THash* objFound ;
	THash* hashNode = (THash*)malloc(sizeof(THash));

	tommy_hashtable* hashTable = (tommy_hashtable*)malloc(sizeof(tommy_hashtable));

	// 262144 = 2^18
	int size = next_power_of_two(100000);
	tommy_hashtable_init(hashTable, size);

	hashNode->id = 227;
	hashNode->data = (void*)5;
	tommy_hashtable_insert(hashTable, &hashNode->node, hashNode, tommy_inthash_u32(hashNode->id));

	hashNode = (THash*)malloc(sizeof(THash));
	hashNode->id = 112;
	hashNode->data = (void*)2;
	tommy_hashtable_insert(hashTable, &hashNode->node, hashNode, tommy_inthash_u32(hashNode->id));

	objFound = (THash*)tommy_hashtable_search(hashTable, compare, &value_to_find, tommy_inthash_u32(value_to_find));

	if (objFound)
		printf("%d\n", objFound->id);
	else
		printf("Nof found\n");


	return 0;
}