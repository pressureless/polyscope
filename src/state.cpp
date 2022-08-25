// Copyright 2017-2019, Nicholas Sharp and the Polyscope contributors. http://polyscope.run.
#include "polyscope/polyscope.h"

namespace polyscope {

namespace state {

bool initialized = false;
std::string backend = "";
float lengthScale = 1.0;
std::tuple<glm::vec3, glm::vec3> boundingBox =
    std::tuple<glm::vec3, glm::vec3>{glm::vec3{-1., -1., -1.}, glm::vec3{1., 1., 1.}};
std::map<std::string, std::map<std::string, Structure*>> structures;
std::function<void()> userCallback = nullptr;
bool doDefaultMouseInteraction = true;

// Lists of things
std::set<Widget*> widgets;
std::vector<SlicePlane*> slicePlanes;

// CHANGED: for DDG
size_t facePickIndStart;
size_t edgePickIndStart;
size_t halfedgePickIndStart;
MeshSubset subset;
int currVertexIndex = -1;
int currFaceIndex = -1;
int currEdgeIndex = -1;
int deleteVertexIndex = -1;
int deleteFaceIndex = -1;
int deleteEdgeIndex = -1;

} // namespace state
} // namespace polyscope
