#include "polyscope/surface_scalar_quantity.h"

#include "polyscope/file_helpers.h"
#include "polyscope/gl/shaders.h"
#include "polyscope/gl/shaders/surface_shaders.h"
#include "polyscope/polyscope.h"

#include "imgui.h"

using std::cout;
using std::endl;

namespace polyscope {

SurfaceScalarQuantity::SurfaceScalarQuantity(std::string name, SurfaceMesh* mesh_, std::string definedOn_,
                                             DataType dataType_)
    : SurfaceQuantityThatDrawsFaces(name, mesh_), dataType(dataType_), definedOn(definedOn_) {
  // Set the default colormap based on what kind of data is given
  switch (dataType) {
  case DataType::STANDARD:
    iColorMap = 0; // viridis
    break;
  case DataType::SYMMETRIC:
    iColorMap = 1; // coolwarm
    break;
  case DataType::MAGNITUDE:
    iColorMap = 2; // blues
    break;
  }
}

void SurfaceScalarQuantity::draw() {}

void SurfaceScalarQuantity::writeToFile(std::string filename) {
  polyscope::warning("Writing to file not yet implemented for this datatype");
}


// Update range uniforms
void SurfaceScalarQuantity::setProgramValues(gl::GLProgram* program) {
  program->setUniform("u_rangeLow", vizRangeLow);
  program->setUniform("u_rangeHigh", vizRangeHigh);
}

void SurfaceScalarQuantity::resetVizRange() {
  switch (dataType) {
  case DataType::STANDARD:
    vizRangeLow = dataRangeLow;
    vizRangeHigh = dataRangeHigh;
    break;
  case DataType::SYMMETRIC: {
    float absRange = std::max(std::abs(dataRangeLow), std::abs(dataRangeHigh));
    vizRangeLow = -absRange;
    vizRangeHigh = absRange;
  } break;
  case DataType::MAGNITUDE:
    vizRangeLow = 0.0;
    vizRangeHigh = dataRangeHigh;
    break;
  }
}

void SurfaceScalarQuantity::drawUI() {
  bool enabledBefore = enabled;
  if (ImGui::TreeNode((name + " (" + definedOn + " scalar)").c_str())) {
    ImGui::Checkbox("Enabled", &enabled);
    ImGui::SameLine();


    // == Options popup
    if (ImGui::Button("Options")) {
      ImGui::OpenPopup("OptionsPopup");
    }
    if (ImGui::BeginPopup("OptionsPopup")) {

      if (ImGui::MenuItem("Write to file")) writeToFile();
      if (ImGui::MenuItem("Reset colormap range")) resetVizRange();

      ImGui::EndPopup();
    }


    { // Set colormap
      ImGui::SameLine();
      ImGui::PushItemWidth(100);
      int iColormapBefore = iColorMap;
      ImGui::Combo("##colormap", &iColorMap, gl::quantitativeColormapNames,
                   IM_ARRAYSIZE(gl::quantitativeColormapNames));
      ImGui::PopItemWidth();
      if (iColorMap != iColormapBefore) {
        parent->deleteProgram();
        hist.updateColormap(gl::quantitativeColormaps[iColorMap]);
      }
    }

    // Draw the histogram of values
    hist.colormapRangeMin = vizRangeLow;
    hist.colormapRangeMax = vizRangeHigh;
    hist.buildUI();

    // Data range
    // Note: %g specifies are generally nicer than %e, but here we don't acutally have a choice. ImGui (for somewhat
    // valid reasons) links the resolution of the slider to the decimal width of the formatted number. When %g formats a
    // number with few decimal places, sliders can break. There is no way to set a minimum number of decimal places with
    // %g, unfortunately.
    {
      switch (dataType) {
      case DataType::STANDARD:
        ImGui::DragFloatRange2("", &vizRangeLow, &vizRangeHigh, (dataRangeHigh - dataRangeLow) / 100., dataRangeLow,
                               dataRangeHigh, "Min: %.3e", "Max: %.3e");
        break;
      case DataType::SYMMETRIC: {
        float absRange = std::max(std::abs(dataRangeLow), std::abs(dataRangeHigh));
        ImGui::DragFloatRange2("##range_symmetric", &vizRangeLow, &vizRangeHigh, absRange / 100., -absRange, absRange,
                               "Min: %.3e", "Max: %.3e");
      } break;
      case DataType::MAGNITUDE: {
        ImGui::DragFloatRange2("##range_mag", &vizRangeLow, &vizRangeHigh, vizRangeHigh / 100., 0.0, dataRangeHigh,
                               "Min: %.3e", "Max: %.3e");
      } break;
      }
    }

    ImGui::TreePop();
  }

  // Enforce exclusivity of enabled surface quantities
  if (!enabledBefore && enabled) {
    parent->setActiveSurfaceQuantity(this);
  }
  if (enabledBefore && !enabled) {
    parent->clearActiveSurfaceQuantity();
  }
}

// ========================================================
// ==========           Vertex Scalar            ==========
// ========================================================

SurfaceScalarVertexQuantity::SurfaceScalarVertexQuantity(std::string name, std::vector<double> values_,
                                                         SurfaceMesh* mesh_, DataType dataType_)
    : SurfaceScalarQuantity(name, mesh_, "vertex", dataType_), values(std::move(values_))

{

  std::vector<double> valsVec;
  std::vector<double> weightsVec;
  for (size_t iV = 0; iV < parent->nVertices; iV++) {
    valsVec.push_back(values[iV]);
    weightsVec.push_back(parent->vertexAreas[iV]);
  }

  hist.updateColormap(gl::quantitativeColormaps[iColorMap]);
  hist.buildHistogram(valsVec, weightsVec);

  std::tie(dataRangeLow, dataRangeHigh) = robustMinMax(valsVec, 1e-5);
  resetVizRange();
}

gl::GLProgram* SurfaceScalarVertexQuantity::createProgram() {
  // Create the program to draw this quantity
  gl::GLProgram* program =
      new gl::GLProgram(&VERTCOLOR_SURFACE_VERT_SHADER, &VERTCOLOR_SURFACE_FRAG_SHADER, gl::DrawMode::Triangles);

  // Fill color buffers
  fillColorBuffers(program);

  return program;
}


void SurfaceScalarVertexQuantity::fillColorBuffers(gl::GLProgram* p) {
  std::vector<double> colorval;
  colorval.reserve(3 * parent->nTriangulationFaces);

  for (TriangulationFace& face : parent->triangulation) {
    for (size_t i = 0; i < 3; i++) {
      size_t vInd = face.vertexInds[i];
      colorval.push_back(values[vInd]);
    }
  }

  // Store data in buffers
  p->setAttribute("a_colorval", colorval);
  p->setTextureFromColormap("t_colormap", *gl::quantitativeColormaps[iColorMap]);
}

void SurfaceScalarVertexQuantity::writeToFile(std::string filename) {

  throw std::runtime_error("not implemented");

  /* TODO
  if (filename == "") {
    filename = promptForFilename();
    if (filename == "") {
      return;
    }
  }

  // For now, just always write scalar to U texture coordinate

  cout << "Writing vertex value to file " << filename << " in U coordinate of texture map" << endl;

  HalfedgeMesh* mesh = parent->mesh;
  CornerData<Vector2> scalarVal(mesh, Vector2{0.0, 0.0});
  for (CornerPtr c : mesh->corners()) {
    scalarVal[c].x = values[c.vertex()];
  }

  WavefrontOBJ::write(filename, *parent->geometry, scalarVal);
  */
}

void SurfaceScalarVertexQuantity::buildVertexInfoGUI(size_t vInd) {
  ImGui::TextUnformatted(name.c_str());
  ImGui::NextColumn();
  ImGui::Text("%g", values[vInd]);
  ImGui::NextColumn();
}

// ========================================================
// ==========            Face Scalar             ==========
// ========================================================

SurfaceScalarFaceQuantity::SurfaceScalarFaceQuantity(std::string name, std::vector<double> values_, SurfaceMesh* mesh_,
                                                     DataType dataType_)
    : SurfaceScalarQuantity(name, mesh_, "face", dataType_), values(std::move(values_))

{

  std::vector<double> valsVec;
  std::vector<double> weightsVec;
  for (size_t fInd = 0; fInd < parent->nFaces; fInd++) {
    valsVec.push_back(values[fInd]);
    weightsVec.push_back(parent->faceAreas[fInd]);
  }

  hist.updateColormap(gl::quantitativeColormaps[iColorMap]);
  hist.buildHistogram(valsVec, weightsVec);

  std::tie(dataRangeLow, dataRangeHigh) = robustMinMax(valsVec, 1e-5);
  resetVizRange();
}

gl::GLProgram* SurfaceScalarFaceQuantity::createProgram() {
  // Create the program to draw this quantity
  gl::GLProgram* program =
      new gl::GLProgram(&VERTCOLOR_SURFACE_VERT_SHADER, &VERTCOLOR_SURFACE_FRAG_SHADER, gl::DrawMode::Triangles);

  // Fill color buffers
  fillColorBuffers(program);

  return program;
}

void SurfaceScalarFaceQuantity::fillColorBuffers(gl::GLProgram* p) {
  std::vector<double> colorval;
  colorval.reserve(3 * parent->nTriangulationFaces);

  for (TriangulationFace& face : parent->triangulation) {
    size_t fInd = face.faceInd;
    for (size_t i = 0; i < 3; i++) {
      colorval.push_back(values[fInd]);
    }
  }

  // Store data in buffers
  p->setAttribute("a_colorval", colorval);
  p->setTextureFromColormap("t_colormap", *gl::quantitativeColormaps[iColorMap]);
}

void SurfaceScalarFaceQuantity::buildFaceInfoGUI(size_t fInd) {
  ImGui::TextUnformatted(name.c_str());
  ImGui::NextColumn();
  ImGui::Text("%g", values[fInd]);
  ImGui::NextColumn();
}


// ========================================================
// ==========            Edge Scalar             ==========
// ========================================================

SurfaceScalarEdgeQuantity::SurfaceScalarEdgeQuantity(std::string name, std::vector<double> values_, SurfaceMesh* mesh_,
                                                     DataType dataType_)
    : SurfaceScalarQuantity(name, mesh_, "edge", dataType_), values(std::move(values_))

{

  std::vector<double> valsVec;
  std::vector<double> weightsVec;
  for (size_t eInd = 0; eInd < parent->nEdges; eInd++) {
    valsVec.push_back(values[eInd]);
    weightsVec.push_back(parent->edgeLengths[eInd]);
  }

  hist.updateColormap(gl::quantitativeColormaps[iColorMap]);
  hist.buildHistogram(valsVec, weightsVec);

  std::tie(dataRangeLow, dataRangeHigh) = robustMinMax(valsVec, 1e-5);
  resetVizRange();
}

gl::GLProgram* SurfaceScalarEdgeQuantity::createProgram() {
  // Create the program to draw this quantity
  gl::GLProgram* program = new gl::GLProgram(&HALFEDGECOLOR_SURFACE_VERT_SHADER, &HALFEDGECOLOR_SURFACE_FRAG_SHADER,
                                             gl::DrawMode::Triangles);

  // Fill color buffers
  fillColorBuffers(program);

  return program;
}

void SurfaceScalarEdgeQuantity::fillColorBuffers(gl::GLProgram* p) {
  std::vector<glm::vec3> colorval;
  colorval.reserve(3 * parent->nTriangulationFaces);

  // TODO this still doesn't look too great on polygon meshes... perhaps compute an average value per edge?

  for (TriangulationFace& face : parent->triangulation) {

    // Compute an average color to use for invalid faces
    double avgVal = 0.0;
    double avgValCount = 0.0;
    for (size_t i = 0; i < 3; i++) {
      size_t eInd = face.edgeInds[i];
      if (eInd != INVALID_IND) {
        avgVal += values[eInd];
        avgValCount++;
      }
    }
    avgVal /= avgValCount;

    // Build a vector of the average or default colors for each edge
    glm::vec3 combinedValues;
    for (size_t i = 0; i < 3; i++) {
      size_t eInd = face.edgeInds[i];
      if (eInd == INVALID_IND) {
        combinedValues[i] = avgVal;
      } else {
        combinedValues[i] = values[eInd];
      }
    }


    for (size_t i = 0; i < 3; i++) {
      colorval.push_back(combinedValues);
    }
  }

  // Store data in buffers
  p->setAttribute("a_colorvals", colorval);
  p->setTextureFromColormap("t_colormap", *gl::quantitativeColormaps[iColorMap]);
}

void SurfaceScalarEdgeQuantity::buildEdgeInfoGUI(size_t eInd) {
  ImGui::TextUnformatted(name.c_str());
  ImGui::NextColumn();
  ImGui::Text("%g", values[eInd]);
  ImGui::NextColumn();
}

// ========================================================
// ==========          Halfedge Scalar           ==========
// ========================================================

SurfaceScalarHalfedgeQuantity::SurfaceScalarHalfedgeQuantity(std::string name, std::vector<double> values_,
                                                             SurfaceMesh* mesh_, DataType dataType_)
    : SurfaceScalarQuantity(name, mesh_, "halfedge", dataType_)

{
  
  std::vector<double> valsVec;
  std::vector<double> weightsVec;
  for(size_t iHe = 0; iHe < parent->nHalfedges; iHe++) {
    valsVec.push_back(values[iHe]);
    weightsVec.push_back(parent->edgeLengths[iHe]);
  }

  hist.updateColormap(gl::quantitativeColormaps[iColorMap]);
  hist.buildHistogram(valsVec, weightsVec);

  std::tie(dataRangeLow, dataRangeHigh) = robustMinMax(valsVec, 1e-5);
  resetVizRange();
}

gl::GLProgram* SurfaceScalarHalfedgeQuantity::createProgram() {
  // Create the program to draw this quantity
  gl::GLProgram* program = new gl::GLProgram(&HALFEDGECOLOR_SURFACE_VERT_SHADER, &HALFEDGECOLOR_SURFACE_FRAG_SHADER,
                                             gl::DrawMode::Triangles);

  // Fill color buffers
  fillColorBuffers(program);

  return program;
}

void SurfaceScalarHalfedgeQuantity::fillColorBuffers(gl::GLProgram* p) {
  std::vector<Vector3> colorval;
  for (FacePtr f : parent->mesh->faces()) {
    // Implicitly triangulate
    double c0, c1;
    size_t iP = 0;
    for (HalfedgePtr he : f.adjacentHalfedges()) {
      VertexPtr v = he.vertex();
      double c2 = values[he.next()];
      if (iP >= 2) {
        colorval.push_back(Vector3{c0, c1, c2});
        colorval.push_back(Vector3{c0, c1, c2});
        colorval.push_back(Vector3{c0, c1, c2});
      }
      if (iP > 2) {
        error("Edge quantities not correct for non-triangular meshes");
      }
      c0 = c1;
      c1 = c2;
      iP++;
    }
  }

  // Store data in buffers
  p->setAttribute("a_colorvals", colorval);
  p->setTextureFromColormap("t_colormap", *gl::quantitativeColormaps[iColorMap]);
}

void SurfaceScalarHalfedgeQuantity::buildInfoGUI(HalfedgePtr he) {
  ImGui::TextUnformatted(name.c_str());
  ImGui::NextColumn();
  ImGui::Text("%g", values[he]);
  ImGui::NextColumn();
}

} // namespace polyscope
