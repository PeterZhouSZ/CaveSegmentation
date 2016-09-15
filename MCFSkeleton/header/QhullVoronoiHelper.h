#pragma once

#include "SurfaceMeshHelper.h"

#define qh_QHpointer 1

#include "libqhullcpp/QhullError.h"
#include "libqhullcpp/QhullQh.h"
#include "libqhullcpp/QhullFacet.h"
#include "libqhullcpp/QhullFacetList.h"
#include "libqhullcpp/QhullFacetSet.h"
#include "libqhullcpp/QhullRidge.h"
#include "libqhullcpp/QhullLinkedList.h"
#include "libqhullcpp/QhullVertex.h"
#include "libqhullcpp/Qhull.h"

#include <cstdio>   /* for printf() of help message */
#include <ostream>
#include <set>

using std::cerr;
using std::cin;
using std::cout;
using std::endl;

using namespace orgQhull;
using namespace SurfaceMesh;

class VoronoiHelper : public SurfaceMeshHelper {

private:
	std::vector< Vector3 > loci;
	std::vector< std::vector<unsigned int> > cells;

	std::vector<int> poleof;				// The index of the voronoi pole the vertex refers to
	std::vector< std::vector<int> > scorr; 	// The index the surface sample to which a pole corresponds to

	std::vector<double> alpha;
	std::vector<double> radii;

	int nvertices, nvornoi;

public:
	VoronoiHelper(SurfaceMeshModel* mesh) : SurfaceMeshHelper(mesh){}

	void computeVoronoiDiagram()
	{
		// Note: we will use entire point cloud indiscriminately 
		mesh->garbage_collection();

		Qhull qhull("", 3, mesh->n_vertices(), &points.data()->x(), "v Qbb");

		// Extract vertices & cell structure
		cells.resize(mesh->n_vertices());
		int i = 0;

		for(QhullFacet f : qhull.facetList())
		{
			if (f.isUpperDelaunay()) continue;

			QhullPoint qhpnt = f.voronoiVertex(qhull.runId());
			Vector3 p(qhpnt[0], qhpnt[1], qhpnt[2]);

			if (!mesh->bbox().contains(p)) continue;

			loci.push_back(p);

			for(QhullVertex v : f.vertices())
				cells[v.point().id()].push_back(i);

			i++;
		}		
		// Used later
		nvertices = this->mesh->n_vertices();
		nvornoi = loci.size();
	}

private:

	struct Spoke {
		double x;
		double y;
		double z;

		Spoke(){ x = y = z = 0; }
		Spoke(double x, double y, double z){
			this->x = x;
			this->y = y;
			this->z = z;
		}

		double angle(const Spoke& s){
			return acos((this->x)*(s.x) + (this->y)*(s.y) + (this->z)*(s.z));
		}
	};

public:
	void searchVoronoiPoles()
	{
		Vector3 surf_vertex;
		Vector3 voro_vertex;
		Vector3 surf_normal;

		poleof = std::vector<int>(nvertices, 0);
		scorr = std::vector< std::vector<int> >(nvornoi, std::vector<int>(4, 0));

		//--- Keeps track of how many 
		//    surface points a voronoi loci has been 
		//    associated with. Assuming general positions 
		//    this number should be always 4.
		std::vector<int> counter(nvornoi, 0);

		//--- For every voronoi cell
		for (unsigned int sidx = 0; (int)sidx < nvertices; sidx++)
		{
			// Retrieve surface vertex
			surf_vertex = points[Vertex(sidx)];
			surf_normal = vnormal[Vertex(sidx)];

			// Index and distance to furthest voronoi loci
			double max_neg_t = DBL_MAX;
			double max_neg_i = 0;

			// For each element of its voronoi cell
			for (int j = 0; j < (int)cells[sidx].size(); j++)
			{
				int vidx = cells[sidx][j];

				voro_vertex = loci[vidx];

				// Mark the fact that (voronoi) vidx corresponds to (surface) sidx 
				// (in the next free location and update this location)
				// the freesub-th correspondent of the voronoi vertex is vidx
				int freesub = counter[vidx];
				if (freesub < 4){
					counter[vidx]++;
					scorr[vidx][freesub] = sidx;
				}

				// Project the loci on the vertex normal & Retain furthest 
				double t = dot(Vector3(voro_vertex - surf_vertex), surf_normal);
				if (t < 0 && t < max_neg_t){
					//drawArea->drawPoint(voro_vertex);
					max_neg_t = t;
					max_neg_i = vidx;
				}
			}

			// Save pole to which surface corresponds
			// Store index (possibly nan!! into buffer)
			poleof[sidx] = max_neg_i;
		}
	}

	void getMedialSpokeAngleAndRadii()
	{
		//--- Temp data
		Vector3 surf_vertex;
		Vector3 voro_vertex;
		Vector3 s; // temp spoke data
		double curralpha;

		alpha = std::vector<double>(nvornoi);
		radii = std::vector<double>(nvornoi);

		//--- For every voronoi vertex
		for (int vidx = 0; vidx < nvornoi; vidx++)
		{
			/// Do not use invalid poles
			//if( ispole.get(vidx) == 0.0 ) continue;

			// Retrieve vertex coordinates
			voro_vertex = loci[vidx];

			// Create the medial spokes
			// General positions => only 4 vertices / loci
			std::vector<Spoke> spokes;
			for (int i_sidx = 0; i_sidx < 4; i_sidx++)
			{
				// Retrieve surface coordinate
				int sidx = scorr[vidx][i_sidx];
				surf_vertex = points[Vertex(sidx)];

				// Create spoke
				s = surf_vertex - voro_vertex;

				// Spoke length (shouldn't this be same as we are voronoi loci?)
				radii[vidx] = s.norm();

				// Normalize spoke
				s.normalize();
				spokes.push_back(Spoke(s[0], s[1], s[2]));
			}


			// Measure largest spoke aperture
			// and store it in output
			double alpha_max = 0;
			for (int i = 0; i<4; i++){
				Spoke& s1 = spokes[i];
				for (int j = 0; j < 4; j++){
					Spoke& s2 = spokes[j];
					curralpha = s1.angle(s2);
					if (curralpha > alpha_max){
						alpha_max = curralpha;
					}
				}
			}
			alpha[vidx] = alpha_max;
		}
	}

	void setToMedial()
	{
		Vector3VertexProperty vpoles = mesh->add_vertex_property<Vector3>("v:pole");

		for(Vertex v : mesh->vertices())
			vpoles[v] = loci[poleof[v.idx()]];		
	}
};
