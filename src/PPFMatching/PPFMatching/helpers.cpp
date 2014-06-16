
#include "WindowGL.h"
#include "helpers.h"
#include "gdiam.hpp"
#include <iostream>
#include <vector>
#include <time.h>
#include <fstream>
#include <opencv2/flann.hpp>

#include "gl_utils.h"

using namespace std;
using namespace cv;

Mat load_ply_simple(const char* fileName, int numVertices, int withNormals)
{
	Mat cloud;

	if (withNormals)
		cloud=Mat(numVertices, 6, CV_32FC1);
	else
		cloud=Mat(numVertices, 3, CV_32FC1);

	ifstream ifs(fileName);

	string str;
	while (str!="end_header")
		getline(ifs, str);

	float dummy =  0;
	for(size_t i = 0; i < numVertices; i++)
	{
		float* data = (float*)(&cloud.data[i*cloud.step[0]]);
		if (withNormals)
		{
			ifs >> data[0] >> data[1] >> data[2] >> data[3] >> data[4] >> data[5];

			// normalize to unit norm
			double norm = sqrt(data[3]*data[3] + data[4]*data[4] + data[5]*data[5]);
			if (norm>0.00001)
			{
				data[3]/=norm;
				data[4]/=norm;
				data[5]/=norm;
			}
		}
		else
		{
			ifs >> data[0] >> data[1] >> data[2];
		}
	}

	//cloud *= 5.0f;
	return cloud;
}



typedef struct
{
	Mat PC;
	TOctreeNode* octree;
	TWindowGL* window;
	int withNormals, withBbox, withOctree;
}TWindowData;

static float light_diffuse[] = {100, 100, 100, 100.0f}; 
static float light_position[] = {-100.0, 100.0, 1.0, 0.0};
static float amb[] =  {0.24, 0.24, 0.24, 0.0};
static float dif[] =  {1.0, 1.0, 1.0, 0.0};

int display(void* UserData)
{
	TWindowData* wd = (TWindowData*)UserData;

	Mat pc = wd->PC;
	TWindowGL* window = wd->window;
	int withNormals = wd->withNormals;
	int withBbox = wd->withBbox;
	int withOctree = wd->withOctree;

	double minVal = 0, maxVal = 0;
	double diam=5;
	
	Mat x,y,z, pcn;
	pc.col(0).copyTo(x);
	pc.col(1).copyTo(y);
	pc.col(2).copyTo(z);

	float cx = cv::mean(x).val[0];
	float cy = cv::mean(y).val[0];
	float cz = cv::mean(z).val[0];

	cv::minMaxIdx(pc, &minVal, &maxVal);

	x=x-cx;
	y=y-cy;
	z=z-cz;
	pcn.create(pc.rows, 3, CV_32FC1);
	x.copyTo(pcn.col(0));
	y.copyTo(pcn.col(1));
	z.copyTo(pcn.col(2));

	cv::minMaxIdx(pcn, &minVal, &maxVal);
	pcn=(float)diam*(pcn)/((float)maxVal-(float)minVal);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(60, 1.0, 1, diam*4);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	gluLookAt (-diam, diam, diam/2,
                0,0, 0,
                0.0, 1.0, 0.0);

	if (window->tracking)
		glRotatef(window->angle, window->rx, window->ry, window->rz);

	glRotatef(window->gangle, window->grx, window->gry, window->grz);
	//else

	/*glRotatef(80,0,0,1);
	glRotatef(40,0,1,0);*/

	// set lights
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_LIGHTING);
    glMaterialfv(GL_FRONT, GL_AMBIENT, amb);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif);

	//glColor4f(1,1,1,1);
	int glSize = 2;
	glLineWidth(glSize);
	glPointSize(glSize);

	/*glDisable(GL_LIGHT0);
	glDisable(GL_LIGHT1);*/
	glDisable(GL_LIGHTING);

	glBegin(GL_POINTS);

	for (int i=0; i!=pcn.rows; i++)
	{
		float* dataPC = (float*)(&pc.data[i*pc.step[0]]);
		float* data = (float*)(&pcn.data[i*pcn.step[0]]);
		if (withNormals)
		{
			glColor4f(dataPC[3],dataPC[4],dataPC[5],1);
			glNormal3f(dataPC[3], dataPC[4], dataPC[5]);
		}

		glVertex3f(data[0], data[1], data[2]);
	}

	glEnd();

	float xRange[2], yRange[2], zRange[2];
	if (withBbox || withOctree)
	{
		compute_obb(pcn, xRange, yRange, zRange);
	}

	if (withBbox)
	{
		glDisable(GL_LIGHT0);
		glDisable(GL_LIGHT1);
		glDisable(GL_LIGHTING);
		glColor4f(0,1,0,1);		
		
		glBegin(GL_LINES);

		glVertex3f(xRange[0], yRange[0], zRange[0]);
		glVertex3f(xRange[1], yRange[0], zRange[0]);

		glVertex3f(xRange[0], yRange[0], zRange[0]);
		glVertex3f(xRange[0], yRange[1], zRange[0]);

		glVertex3f(xRange[0], yRange[0], zRange[0]);
		glVertex3f(xRange[0], yRange[0], zRange[1]);

		glVertex3f(xRange[1], yRange[1], zRange[1]);
		glVertex3f(xRange[1], yRange[1], zRange[0]);

		glVertex3f(xRange[1], yRange[1], zRange[1]);
		glVertex3f(xRange[1], yRange[0], zRange[1]);

		glVertex3f(xRange[1], yRange[1], zRange[1]);
		glVertex3f(xRange[0], yRange[1], zRange[1]);

		glVertex3f(xRange[1], yRange[0], zRange[0]);
		glVertex3f(xRange[1], yRange[0], zRange[1]);

		glVertex3f(xRange[1], yRange[0], zRange[0]);
		glVertex3f(xRange[1], yRange[1], zRange[0]);

		glVertex3f(xRange[0], yRange[1], zRange[0]);
		glVertex3f(xRange[0], yRange[1], zRange[1]);

		glVertex3f(xRange[0], yRange[1], zRange[0]);
		glVertex3f(xRange[1], yRange[1], zRange[0]);

		glVertex3f(xRange[0], yRange[0], zRange[1]);
		glVertex3f(xRange[0], yRange[1], zRange[1]);

		glVertex3f(xRange[0], yRange[0], zRange[1]);
		glVertex3f(xRange[1], yRange[0], zRange[1]);

		glEnd();
	}

	if (withOctree)
	{
		float cx = (xRange[1] + xRange[0])*0.5f;
		float cy = (yRange[1] + yRange[0])*0.5f;
		float cz = (zRange[1] + zRange[0])*0.5f;
		wd->octree = Mat2Octree(pcn);
		glDisable(GL_LIGHT0);
		glDisable(GL_LIGHT1);
		glDisable(GL_LIGHTING);
		glSize = 1;
		glLineWidth(glSize);
		glPointSize(glSize);
		glColor4f(1,1,1,1);
		draw_octree(wd->octree, cx, cy, cz, xRange[1] - xRange[0], yRange[1] - yRange[0], zRange[1] - zRange[0]);
		t_octree_destroy(wd->octree);
	}

	//SwapBuffers(window->hDC);

	return 0;
}

TOctreeNode* Mat2Octree(Mat pc)
{
	float xRange[2], yRange[2], zRange[2];
	compute_obb(pc, xRange, yRange, zRange);

	float cx = (xRange[1] + xRange[0])*0.5f;
	float cy = (yRange[1] + yRange[0])*0.5f;
	float cz = (zRange[1] + zRange[0])*0.5f;

	float maxDim = MAX( MAX(xRange[1], yRange[1]), zRange[1]);
	float minDim = MIN( MIN(xRange[1], yRange[1]), zRange[1]);

	float root[3]={cx, cy, cz};
	float half_dim[3]={(xRange[1]-xRange[0])*0.5, (yRange[1]-yRange[0])*0.5, (zRange[1]-zRange[0])*0.5};

	TOctreeNode * oc = new TOctreeNode();
	t_octree_init(oc, root, half_dim);

	int nPoints = pc.rows;
	for(int i=0; i<nPoints; ++i) 
	{
		float* data = (float*)(&pc.data[i*pc.step[0]]);
		t_octree_insert(oc, data);
	}

	return oc;
}

void* visualize_pc(Mat pc, int withNormals, int withBbox, int withOctree, char* Title)
{
	int width = 1024;
	int height = 1024;

	TWindowGL* window=(TWindowGL*)calloc(1, sizeof(TWindowGL));
	int status=CreateGLWindow(window, Title, 300, 300, width, height, 24);

	MoveGLWindow(window, 300, 300);
	update_window(window);

	glEnable3D(45, 1, 5, width, height);

	TWindowData* wd = new TWindowData();
	wd->PC = pc;
	wd->window = window;
	wd->withNormals=withNormals;
	wd->withBbox=withBbox;
	wd->withOctree = withOctree;
	wd->octree=0;

	if (withOctree)
	{
		wd->octree = Mat2Octree(pc);
	}

	//draw_custom_gl_scene(window, display, wd);
	register_custom_gl_scene(window, display, wd);

	//update_window(window);
	wait_window(window);

	return (void*)window;
}


Mat sample_pc_uniform(Mat PC, int sampleStep)
{
	int numRows = PC.rows/sampleStep;
	Mat sampledPC = Mat(numRows, PC.cols, PC.type());

	int c=0;
	for (int i=0; i<PC.rows && c<numRows; i+=sampleStep)
	{
		PC.row(i).copyTo(sampledPC.row(c++));
	}

	return sampledPC;
}

// not yet implemented
Mat sample_pc_perfect_uniform(Mat PC, int sampleStep)
{
	// make a symmetric square matrix for sampling
	int numPoints = PC.rows;
	int numPointsSampled = numPoints/sampleStep;

	int n = sqrt((double)numPoints);
	int numTotalPerfect = n*n;

	
	return Mat();
}


void* index_pc_flann(Mat pc, cvflann::Matrix<float>& data)
{	
	cvflann::AutotunedIndexParams params;
	
	data = cvflann::Matrix<float>( (float*)pc.data, pc.rows, pc.cols );
	
	cvflann::Index < Distance_32F>* flannIndex = new cvflann::Index< Distance_32F >(data, params);

	return (void*)flannIndex;
}	

Mat sample_pc_kd_tree(Mat pc, float radius, int numNeighbors)
{
	cvflann::AutotunedIndexParams params;
	cvflann::SearchParams searchParams;
	cvflann::Matrix<float> data;
	cvflann::Index < Distance_32F>* flannIndex = (cvflann::Index < Distance_32F>*)index_pc_flann(pc, data);
	
	cv::Mat1i ind(pc.rows, numNeighbors);
	cvflann::Matrix<int> indices((int*) ind.data, ind.rows, ind.cols);
	cvflann::Matrix<float> dists(new float[pc.rows*numNeighbors], pc.rows, numNeighbors);

	flannIndex->radiusSearch(data, indices, dists, radius, searchParams);

	return Mat();	
}

Mat sample_pc_octree(Mat pc, float xrange[2], float yrange[2], float zrange[2], float resolution)
{
	TOctreeNode *oc = Mat2Octree(pc);

	float xstep = (xrange[1]-xrange[0]) * resolution;
	float ystep = (yrange[1]-yrange[0]) * resolution;
	float zstep = (zrange[1]-zrange[0]) * resolution;

	float pdx = xrange[0], pdy=yrange[0], pdz=zrange[0];
	float dx=pdx+xstep, dy=pdy+ystep, dz=pdz+zstep;

	int numPoints = 0, c=0;
	int interpNormals = (pc.cols==6);		

	// count the number of points
	while (pdx<=xrange[1])
	{
		pdy=yrange[0]; 
		while (pdy<=yrange[1])
		{
			pdz=zrange[0];
			while (pdz<=zrange[1])
			{
				numPoints++;
				pdz+=zstep;
			}
			pdy+=ystep;
		}
		pdx+=xstep;
	}

	Mat pcSampled = Mat(numPoints, pc.cols, CV_32FC1);

	pdx = xrange[0]; 
	dx=pdx+xstep;  

	//while (dx<xrange[1] && dy<yrange[1] && dz<zrange[1])
	// query discrete bounding boxes over the octree
	while (pdx<=xrange[1])
	{
		float xbox[2] = {pdx, dx};
		pdy=yrange[0]; 
		dy=pdy+ystep;
		while (pdy<=yrange[1])
		{
			float ybox[2] = {pdy, dy};
			pdz=zrange[0];
			dz=pdz+zstep;
			while (pdz<=zrange[1])
			{
				float zbox[2] = {pdz, dz};
				int j;
				float px=0, py=0, pz=0;
				float nx=0, ny=0, nz=0;
				std::vector<float*> results;
				float *pcData = (float*)(&pcSampled.data[c*pcSampled.step[0]]);

				t_octree_query_in_bbox ( oc, xbox, ybox, zbox, results );

				if (results.size())
				{
					if (!interpNormals)
					{
						for (j=0; j<results.size(); j++)
						{
							px += results[j][0];
							py += results[j][1];
							pz += results[j][2];
						}

						px/=(float)results.size();
						py/=(float)results.size();
						pz/=(float)results.size();

						pcData[0]=px;
						pcData[1]=py;
						pcData[2]=pz;
					}
					else
					{
						for (j=0; j<results.size(); j++)
						{
							px += results[j][0];
							py += results[j][1];
							pz += results[j][2];
							nx += results[j][3];
							ny += results[j][4];
							nz += results[j][5];
						}

						px/=(float)results.size();
						py/=(float)results.size();
						pz/=(float)results.size();
						nx/=(float)results.size();
						ny/=(float)results.size();
						nz/=(float)results.size();

						pcData[0]=px;
						pcData[1]=py;
						pcData[2]=pz;
						pcData[3]=nx;
						pcData[4]=ny;
						pcData[5]=nz;
					}

					c++;
				}

				results.clear();

				pdz=dz;
				dz+=zstep;
			}
			pdy=dy;
			dy+=ystep; 
		}
		pdx=dx;
		dx+=xstep; 
	}


	t_octree_destroy(oc);

	pcSampled=pcSampled.rowRange(0, c);

	return pcSampled;
}

void shuffle(int *array, size_t n)
{
	size_t i;
	for (i = 0; i < n - 1; i++) 
	{
		size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
		int t = array[j];
		array[j] = array[i];
		array[i] = t;
	}
}

Mat sample_pc_random(Mat PC, int numPoints)
{
	Mat sampledPC = Mat(numPoints, PC.cols, PC.type());

	// This is a slight inefficient way of doing it. Will optimize in final version.
	srand(time(0));
	int* randInd = new int[PC.rows];
	for (int i=0; i<PC.rows; i++)
		randInd[i]=i;
	shuffle(randInd, PC.rows);

	for (int i=0; i<numPoints; i++)
	{
		PC.row(randInd[i]).copyTo(sampledPC.row(i));
	}

	delete[] (randInd);

	return sampledPC;
}

// compute the oriented bounding box
void compute_obb(Mat pc, float xRange[2], float yRange[2], float zRange[2])
{
	Mat pcPts = pc.colRange(0, 3);
	int num = pcPts.rows;

	float* points = (float*)pcPts.data;
    GPointPair   pair;

    //printf( "Axis parallel bounding box\n" );
    GBBox   bbx;
    bbx.init();
    for  ( int  ind = 0; ind < num; ind++ )
        bbx.bound( (float*)(pcPts.data + (ind * pcPts.step)) );
    //bbx.dump();

	xRange[0]=bbx.min_coord(0);
	yRange[0]=bbx.min_coord(1);
	zRange[0]=bbx.min_coord(2);

	xRange[1]=bbx.max_coord(0);
	yRange[1]=bbx.max_coord(1);
	zRange[1]=bbx.max_coord(2);
}