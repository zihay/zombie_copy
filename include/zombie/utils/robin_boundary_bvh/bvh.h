// This file extends the 'Bvh' structure from the FCPW library to support Robin
// boundary conditions. Users of Zombie need not interact with this file directly.

#pragma once

#include <zombie/utils/robin_boundary_bvh/geometry.h>

namespace zombie {

using namespace fcpw;

template<size_t DIM>
struct RobinBvhNode {
    // constructor
    RobinBvhNode(): nReferences(0) {}

    // members
    BoundingBox<DIM> box;
    BoundingCone<DIM> cone;
    union {
        int referenceOffset;
        int secondChildOffset;
    };
    int nReferences;
    float minRobinCoeff;
    float maxRobinCoeff;
};

template<size_t DIM, typename NodeType, typename PrimitiveType>
class RobinBvh: public Bvh<DIM, NodeType, PrimitiveType> {
public:
    // constructor
    RobinBvh(const CostHeuristic& costHeuristic_,
             std::vector<PrimitiveType *>& primitives_,
             std::vector<SilhouettePrimitive<DIM> *>& silhouettes_,
             SortPositionsFunc<DIM, RobinBvhNode<DIM>, PrimitiveType, SilhouettePrimitive<DIM>> sortPositions_={},
             bool packLeaves_=false, int leafSize_=4, int nBuckets_=8);

    // refits the bvh
    void refit();

    // updates robin coefficient for each primitive and node
    void updateRobinCoefficients(const std::vector<float>& minCoeffValues,
                                 const std::vector<float>& maxCoeffValues);

    // computes the squared Robin star radius
    int computeSquaredStarRadius(BoundingSphere<DIM>& s,
                                 bool flipNormalOrientation,
                                 float silhouettePrecision) const;

protected:
    // assigns geometric data (e.g. cones and robin coeffs) to nodes
    void assignGeometricDataToNodes(const std::function<bool(float, int)>& ignoreSilhouette);

    // checks whether the node should be visited during traversal
    bool visitNode(const BoundingSphere<DIM>& s, int nodeIndex,
                   float& r2MinBound, float& r2MaxBound,
                   bool& hasSilhouette) const;
};

template<size_t DIM, typename PrimitiveType>
std::unique_ptr<RobinBvh<DIM, RobinBvhNode<DIM>, PrimitiveType>> initializeRobinBvh(
                                                    PolygonSoup<DIM>& soup,
                                                    std::vector<PrimitiveType *>& primitives,
                                                    std::vector<SilhouettePrimitive<DIM> *>& silhouettes,
                                                    bool printStats=false, bool packLeaves=false, int leafSize=4);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation

template<size_t DIM, typename NodeType, typename PrimitiveType, typename SilhouetteType>
inline void sortRobinSoupPositions(const std::vector<NodeType>& flatTree,
                                   std::vector<PrimitiveType *>& primitives,
                                   std::vector<SilhouetteType *>& silhouettes,
                                   PolygonSoup<DIM>& soup)
{
    // do nothing
}

template<>
inline void sortRobinSoupPositions<2, RobinBvhNode<2>, RobinLineSegment, SilhouettePrimitive<2>>(
                                                    const std::vector<RobinBvhNode<2>>& flatTree,
                                                    std::vector<RobinLineSegment *>& lineSegments,
                                                    std::vector<SilhouettePrimitive<2> *>& silhouettes,
                                                    PolygonSoup<2>& soup)
{
    sortLineSegmentSoupPositions<RobinBvhNode<2>, RobinLineSegment>(flatTree, lineSegments, soup);
}

template<>
inline void sortRobinSoupPositions<3, RobinBvhNode<3>, RobinTriangle, SilhouettePrimitive<3>>(
                                                    const std::vector<RobinBvhNode<3>>& flatTree,
                                                    std::vector<RobinTriangle *>& triangles,
                                                    std::vector<SilhouettePrimitive<3> *>& silhouettes,
                                                    PolygonSoup<3>& soup)
{
    sortTriangleSoupPositions<RobinBvhNode<3>, RobinTriangle>(flatTree, triangles, soup);
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline void assignGeometricDataToNodesRecursive(const std::vector<PrimitiveType *>& primitives,
                                                const std::vector<Vector<DIM>>& primitiveNormals,
                                                std::vector<NodeType>& flatTree, int start, int end)
{
    // compute bounding cone axis and min and max robin coefficients for node
    BoundingCone<DIM> cone;
    NodeType& node(flatTree[start]);
    Vector<DIM> centroid = node.box.centroid();
    float minRobinCoeff = maxFloat;
    float maxRobinCoeff = minFloat;
    bool allPrimitivesHaveAdjacentFaces = true;

    for (int i = start; i < end; i++) {
        NodeType& childNode(flatTree[i]);

        for (int j = 0; j < childNode.nReferences; j++) { // leaf node if nReferences > 0
            int referenceIndex = childNode.referenceOffset + j;
            const PrimitiveType *prim = primitives[referenceIndex];

            cone.axis += primitiveNormals[referenceIndex];
            minRobinCoeff = std::min(minRobinCoeff, prim->minRobinCoeff);
            maxRobinCoeff = std::max(maxRobinCoeff, prim->maxRobinCoeff);
            for (int k = 0; k < DIM; k++) {
                Vector<DIM> p = Vector<DIM>::Zero();
                if (DIM == 2) {
                    p = prim->soup->positions[prim->indices[k]];

                } else if (DIM == 3) {
                    int I = prim->indices[k];
                    int J = prim->indices[(k + 1)%3];
                    p = 0.5f*(prim->soup->positions[I] + prim->soup->positions[J]);
                }

                cone.radius = std::max(cone.radius, (p - centroid).norm());
                allPrimitivesHaveAdjacentFaces = allPrimitivesHaveAdjacentFaces && prim->hasAdjacentFace[k];
            }
        }
    }

    // compute bounding cone angle
    node.minRobinCoeff = minRobinCoeff;
    node.maxRobinCoeff = maxRobinCoeff;

    if (!allPrimitivesHaveAdjacentFaces) {
        node.cone.halfAngle = M_PI;

    } else {
        float axisNorm = cone.axis.norm();
        if (axisNorm > epsilon) {
            cone.axis /= axisNorm;
            cone.halfAngle = 0.0f;

            for (int i = start; i < end; i++) {
                NodeType& childNode(flatTree[i]);

                for (int j = 0; j < childNode.nReferences; j++) { // leaf node if nReferences > 0
                    int referenceIndex = childNode.referenceOffset + j;
                    const PrimitiveType *prim = primitives[referenceIndex];

                    const Vector<DIM>& nj = primitiveNormals[referenceIndex];
                    float angle = std::acos(std::max(-1.0f, std::min(1.0f, cone.axis.dot(nj))));
                    cone.halfAngle = std::max(cone.halfAngle, angle);

                    for (int k = 0; k < DIM; k++) {
                        const Vector<DIM>& nk = prim->n[k];
                        float angle = std::acos(std::max(-1.0f, std::min(1.0f, cone.axis.dot(nk))));
                        cone.halfAngle = std::max(cone.halfAngle, angle);
                    }
                }
            }

            node.cone = cone;
        }
    }

    // recurse on children
    if (node.nReferences == 0) { // not a leaf
        assignGeometricDataToNodesRecursive<DIM, NodeType, PrimitiveType>(
            primitives, primitiveNormals, flatTree, start + 1, start + node.secondChildOffset);
        assignGeometricDataToNodesRecursive<DIM, NodeType, PrimitiveType>(
            primitives, primitiveNormals, flatTree, start + node.secondChildOffset, end);
    }
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline void RobinBvh<DIM, NodeType, PrimitiveType>::assignGeometricDataToNodes(const std::function<bool(float, int)>& ignoreSilhouette)
{
    // precompute normals for each primitive
    using BvhBase = Bvh<DIM, NodeType, PrimitiveType>;
    int nNodes = (int)BvhBase::flatTree.size();
    int nPrimitives = (int)BvhBase::primitives.size();
    std::vector<Vector<DIM>> primitiveNormals(nPrimitives, Vector<DIM>::Zero());

    for (int i = 0; i < nNodes; i++) {
        const NodeType& node(BvhBase::flatTree[i]);

        for (int j = 0; j < node.nReferences; j++) { // leaf node if nReferences > 0
            int referenceIndex = node.referenceOffset + j;
            const PrimitiveType *prim = BvhBase::primitives[referenceIndex];

            primitiveNormals[referenceIndex] = prim->normal(true);
        }
    }

    // compute bounding cones recursively
    if (nNodes > 0) {
        assignGeometricDataToNodesRecursive<DIM, NodeType, PrimitiveType>(
            BvhBase::primitives, primitiveNormals, BvhBase::flatTree, 0, nNodes);
    }
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline RobinBvh<DIM, NodeType, PrimitiveType>::RobinBvh(const CostHeuristic& costHeuristic_,
                                                        std::vector<PrimitiveType *>& primitives_,
                                                        std::vector<SilhouettePrimitive<DIM> *>& silhouettes_,
                                                        SortPositionsFunc<DIM, RobinBvhNode<DIM>, PrimitiveType, SilhouettePrimitive<DIM>> sortPositions_,
                                                        bool packLeaves_, int leafSize_, int nBuckets_):
Bvh<DIM, NodeType, PrimitiveType, SilhouettePrimitive<DIM>>(
    costHeuristic_, primitives_, silhouettes_, sortPositions_, {}, packLeaves_, leafSize_, nBuckets_)
{
    // assigns geometric data (i.e., cones and robin coefficients) to nodes
    assignGeometricDataToNodes({});
}

template<size_t DIM>
inline void mergeBoundingCones(const RobinBvhNode<DIM>& left, const RobinBvhNode<DIM>& right, RobinBvhNode<DIM>& node)
{
    node.cone = mergeBoundingCones<DIM>(left.cone, right.cone,
                                        left.box.centroid(),
                                        right.box.centroid(),
                                        node.box.centroid());
}

template<size_t DIM, typename PrimitiveType>
inline BoundingCone<DIM> computeBoundingConeForPrimitives(const std::vector<PrimitiveType *>& primitives,
                                                          const Vector<DIM>& centroid,
                                                          int nReferences, int referenceOffset)
{
    BoundingCone<DIM> cone;
    bool allPrimitivesHaveAdjacentFaces = true;

    for (int p = 0; p < nReferences; p++) {
        int referenceIndex = referenceOffset + p;
        const PrimitiveType *prim = primitives[referenceIndex];

        cone.axis += prim->normal();
        for (int k = 0; k < DIM; k++) {
            Vector<DIM> p = Vector<DIM>::Zero();
            if (DIM == 2) {
                p = prim->soup->positions[prim->indices[k]];

            } else if (DIM == 3) {
                int I = prim->indices[k];
                int J = prim->indices[(k + 1)%3];
                p = 0.5f*(prim->soup->positions[I] + prim->soup->positions[J]);
            }

            cone.radius = std::max(cone.radius, (p - centroid).norm());
            allPrimitivesHaveAdjacentFaces = allPrimitivesHaveAdjacentFaces && prim->hasAdjacentFace[k];
        }
    }

    // compute bounding cone angle
    if (!allPrimitivesHaveAdjacentFaces) {
        cone.halfAngle = M_PI;

    } else {
        float axisNorm = cone.axis.norm();
        if (axisNorm > epsilon) {
            cone.axis /= axisNorm;
            cone.halfAngle = 0.0f;

            for (int p = 0; p < nReferences; p++) {
                int referenceIndex = referenceOffset + p;
                const PrimitiveType *prim = primitives[referenceIndex];

                Vector<DIM> nj = prim->normal();
                float angle = std::acos(std::max(-1.0f, std::min(1.0f, cone.axis.dot(nj))));
                cone.halfAngle = std::max(cone.halfAngle, angle);

                for (int k = 0; k < DIM; k++) {
                    const Vector<DIM>& nk = prim->n[k];
                    float angle = std::acos(std::max(-1.0f, std::min(1.0f, cone.axis.dot(nk))));
                    cone.halfAngle = std::max(cone.halfAngle, angle);
                }
            }
        }
    }

    return cone;
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline void refitRecursive(const std::vector<PrimitiveType *>& primitives,
                           std::vector<NodeType>& flatTree, int nodeIndex)
{
    NodeType& node(flatTree[nodeIndex]);

    if (node.nReferences == 0) { // not a leaf
        refitRecursive<DIM, NodeType, PrimitiveType>(primitives, flatTree, nodeIndex + 1);
        refitRecursive<DIM, NodeType, PrimitiveType>(primitives, flatTree, nodeIndex + node.secondChildOffset);

        // merge left and right child bounding boxes
        node.box = flatTree[nodeIndex + 1].box;
        node.box.expandToInclude(flatTree[nodeIndex + node.secondChildOffset].box);

        // merge left and right child bounding cones
        mergeBoundingCones(flatTree[nodeIndex + 1], flatTree[nodeIndex + node.secondChildOffset], node);

    } else { // leaf
        // compute bounding box
        node.box = BoundingBox<DIM>();
        for (int p = 0; p < node.nReferences; p++) {
            int referenceIndex = node.referenceOffset + p;
            const PrimitiveType *prim = primitives[referenceIndex];

            node.box.expandToInclude(prim->boundingBox());
        }

        // compute bounding cone
        Vector<DIM> centroid = node.box.centroid();
        node.cone = computeBoundingConeForPrimitives<DIM, PrimitiveType>(
            primitives, centroid, node.nReferences, node.referenceOffset);
    }
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline void RobinBvh<DIM, NodeType, PrimitiveType>::refit()
{
    using BvhBase = Bvh<DIM, NodeType, PrimitiveType>;
    int nNodes = (int)BvhBase::flatTree.size();

    if (nNodes > 0) {
        refitRecursive<DIM, NodeType, PrimitiveType>(BvhBase::primitives, BvhBase::flatTree, 0);
    }
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline std::pair<float, float> updateRobinCoefficientsRecursive(const std::vector<PrimitiveType *>& primitives,
                                                                std::vector<NodeType>& flatTree, int nodeIndex)
{
    NodeType& node(flatTree[nodeIndex]);
    node.minRobinCoeff = maxFloat;
    node.maxRobinCoeff = minFloat;

    if (node.nReferences == 0) { // not a leaf
        std::pair<float, float> minMaxRobinCoeffsLeft =
            updateRobinCoefficientsRecursive<DIM, NodeType, PrimitiveType>(
                primitives, flatTree, nodeIndex + 1);
        std::pair<float, float> minMaxRobinCoeffsRight =
            updateRobinCoefficientsRecursive<DIM, NodeType, PrimitiveType>(
                primitives, flatTree, nodeIndex + node.secondChildOffset);

        node.minRobinCoeff = std::min(minMaxRobinCoeffsLeft.first, minMaxRobinCoeffsRight.first);
        node.maxRobinCoeff = std::max(minMaxRobinCoeffsLeft.second, minMaxRobinCoeffsRight.second);

    } else { // leaf
        for (int i = 0; i < node.nReferences; i++) {
            int referenceIndex = node.referenceOffset + i;
            const PrimitiveType *prim = primitives[referenceIndex];

            node.minRobinCoeff = std::min(node.minRobinCoeff, prim->minRobinCoeff);
            node.maxRobinCoeff = std::max(node.maxRobinCoeff, prim->maxRobinCoeff);
        }
    }

    return std::make_pair(node.minRobinCoeff, node.maxRobinCoeff);
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline void RobinBvh<DIM, NodeType, PrimitiveType>::updateRobinCoefficients(const std::vector<float>& minCoeffValues,
                                                                            const std::vector<float>& maxCoeffValues)
{
    // update robin coefficients for primitives
    using BvhBase = Bvh<DIM, NodeType, PrimitiveType>;
    int nNodes = (int)BvhBase::flatTree.size();
    int nPrimitives = (int)BvhBase::primitives.size();
    for (int p = 0; p < nPrimitives; p++) {
        PrimitiveType *prim = BvhBase::primitives[p];

        prim->minRobinCoeff = minCoeffValues[prim->getIndex()];
        prim->maxRobinCoeff = maxCoeffValues[prim->getIndex()];
    }

    // update robin coefficients for nodes
    if (nNodes > 0) {
        updateRobinCoefficientsRecursive<DIM, NodeType, PrimitiveType>(
            BvhBase::primitives, BvhBase::flatTree, 0);
    }
}

template<size_t DIM>
inline float computeNodeSquaredStarRadiusBound(float r0, float r1, float robinCoeff, float cosTheta)
{
    std::cerr << "computeNodeSquaredStarRadiusBound(): DIM: " << DIM << " not supported" << std::endl;
    exit(EXIT_FAILURE);

    return 0.0f;
}

template<>
inline float computeNodeSquaredStarRadiusBound<2>(float r0, float r1, float robinCoeff, float cosTheta)
{
    float rBound = r0*std::exp(cosTheta/(robinCoeff*r1));
    return rBound*rBound;
}

template<>
inline float computeNodeSquaredStarRadiusBound<3>(float r0, float r1, float robinCoeff, float cosTheta)
{
    if (r1 < cosTheta/robinCoeff) {
        return maxFloat;
    }

    float rBound = r0/(1.0f - (cosTheta/(robinCoeff*r1)));
    return rBound*rBound;
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline bool RobinBvh<DIM, NodeType, PrimitiveType>::visitNode(const BoundingSphere<DIM>& s, int nodeIndex,
                                                              float& r2MinBound, float& r2MaxBound,
                                                              bool& hasSilhouette) const
{
    using BvhBase = Bvh<DIM, NodeType, PrimitiveType>;
    const NodeType& node(BvhBase::flatTree[nodeIndex]);
    hasSilhouette = true;

    if (node.box.overlap(s, r2MinBound, r2MaxBound)) {
        if (node.minRobinCoeff < maxFloat - epsilon) { // early out for Dirichlet case
            // perform silhouette test for Neuamnn and Robin cases
            float maximalAngles[2];
            if (node.cone.overlap(s.c, node.box, r2MinBound, maximalAngles[0], maximalAngles[1])) {
                r2MaxBound = maxFloat;

            } else {
                hasSilhouette = false;
                if (node.maxRobinCoeff > epsilon) {
                    // Robin case: compute radius bounds
                    float rMin = std::sqrt(r2MinBound);
                    float rMax = std::sqrt(r2MaxBound);
                    float minAbsCosTheta = std::min(std::fabs(std::cos(maximalAngles[0])),
                                                    std::fabs(std::cos(maximalAngles[1])));
                    float maxAbsCosTheta = 1.0f; // assume maxCosTheta = 1.0f for simplicity
                    r2MinBound = computeNodeSquaredStarRadiusBound<DIM>(rMin, rMax, node.maxRobinCoeff, minAbsCosTheta);
                    r2MaxBound = computeNodeSquaredStarRadiusBound<DIM>(rMax, rMin, node.minRobinCoeff, maxAbsCosTheta);

                } else {
                    // Neumann case: r2MinBound becomes infinite, which means the node will not be visited
                    return false;
                }
            }
        }

        return true;
    }

    return false;
}

template<size_t DIM, typename NodeType, typename PrimitiveType>
inline int RobinBvh<DIM, NodeType, PrimitiveType>::computeSquaredStarRadius(BoundingSphere<DIM>& s,
                                                                            bool flipNormalOrientation,
                                                                            float silhouettePrecision) const
{
    using BvhBase = Bvh<DIM, NodeType, PrimitiveType>;
    TraversalStack subtree[FCPW_BVH_MAX_DEPTH];
    float boxHits[4];
    bool hasSilhouettes[2];
    int nodesVisited = 0;

    if (visitNode(s, 0, boxHits[0], boxHits[1], hasSilhouettes[0])) {
        subtree[0].node = 0;
        subtree[0].distance = boxHits[0];
        int stackPtr = 0;

        while (stackPtr >= 0) {
            // pop off the next node to work on
            int nodeIndex = subtree[stackPtr].node;
            float currentDist = subtree[stackPtr].distance;
            stackPtr--;

            // if this node is further than the current radius estimate, continue
            if (std::fabs(currentDist) > s.r2) continue;
            const NodeType& node(BvhBase::flatTree[nodeIndex]);

            // is leaf -> compute squared distance
            if (node.nReferences > 0) {
                for (int p = 0; p < node.nReferences; p++) {
                    int referenceIndex = node.referenceOffset + p;
                    const PrimitiveType *prim = BvhBase::primitives[referenceIndex];
                    nodesVisited++;

                    // assume we are working only with Robin primitives
                    prim->computeSquaredStarRadius(s, flipNormalOrientation, silhouettePrecision, currentDist >= 0.0f);
                }

            } else { // not a leaf
                bool hit0 = visitNode(s, nodeIndex + 1, boxHits[0], boxHits[1], hasSilhouettes[0]);
                if (hit0) s.r2 = std::min(s.r2, boxHits[1]);

                bool hit1 = visitNode(s, nodeIndex + node.secondChildOffset, boxHits[2], boxHits[3], hasSilhouettes[1]);
                if (hit1) s.r2 = std::min(s.r2, boxHits[3]);

                // is there overlap with both nodes?
                if (hit0 && hit1) {
                    // we assume that the left child is a closer hit...
                    int closer = nodeIndex + 1;
                    int other = nodeIndex + node.secondChildOffset;

                    // ... if the right child was actually closer, swap the relavent values
                    if (boxHits[0] == 0.0f && boxHits[2] == 0.0f) {
                        if (boxHits[3] < boxHits[1]) {
                            std::swap(hasSilhouettes[0], hasSilhouettes[1]);
                            std::swap(closer, other);
                        }

                    } else if (boxHits[2] < boxHits[0]) {
                        std::swap(boxHits[0], boxHits[2]);
                        std::swap(hasSilhouettes[0], hasSilhouettes[1]);
                        std::swap(closer, other);
                    }

                    // it's possible that the nearest object is still in the other side, but we'll
                    // check the farther-away node later...

                    // push the farther first, then the closer
                    stackPtr++;
                    subtree[stackPtr].node = other;
                    subtree[stackPtr].distance = boxHits[2]*(hasSilhouettes[1] ? 1.0f : -1.0f);

                    stackPtr++;
                    subtree[stackPtr].node = closer;
                    subtree[stackPtr].distance = boxHits[0]*(hasSilhouettes[0] ? 1.0f : -1.0f);

                } else if (hit0) {
                    stackPtr++;
                    subtree[stackPtr].node = nodeIndex + 1;
                    subtree[stackPtr].distance = boxHits[0]*(hasSilhouettes[0] ? 1.0f : -1.0f);

                } else if (hit1) {
                    stackPtr++;
                    subtree[stackPtr].node = nodeIndex + node.secondChildOffset;
                    subtree[stackPtr].distance = boxHits[2]*(hasSilhouettes[1] ? 1.0f : -1.0f);
                }

                nodesVisited++;
            }
        }
    }

    return nodesVisited;
}

template<size_t DIM, typename PrimitiveType>
std::unique_ptr<RobinBvh<DIM, RobinBvhNode<DIM>, PrimitiveType>> initializeRobinBvh(
                                                    PolygonSoup<DIM>& soup,
                                                    std::vector<PrimitiveType *>& primitives,
                                                    std::vector<SilhouettePrimitive<DIM> *>& silhouettes,
                                                    bool printStats, bool packLeaves, int leafSize)
{
    if (primitives.size() > 0) {
        using namespace std::chrono;
        high_resolution_clock::time_point t1 = high_resolution_clock::now();

        using SortPositionsFunc = std::function<void(const std::vector<RobinBvhNode<DIM>>&,
                                                     std::vector<PrimitiveType *>&,
                                                     std::vector<SilhouettePrimitive<DIM> *>&)>;
        SortPositionsFunc sortPositions = std::bind(
            &sortRobinSoupPositions<DIM, RobinBvhNode<DIM>, PrimitiveType, SilhouettePrimitive<DIM>>,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::ref(soup));
        std::unique_ptr<RobinBvh<DIM, RobinBvhNode<DIM>, PrimitiveType>> bvh(
            new RobinBvh<DIM, RobinBvhNode<DIM>, PrimitiveType>(
                fcpw::CostHeuristic::SurfaceArea,
                primitives, silhouettes,
                sortPositions, packLeaves, leafSize
            )
        );

        if (printStats) {
            high_resolution_clock::time_point t2 = high_resolution_clock::now();
            duration<double> timeSpan = duration_cast<duration<double>>(t2 - t1);
            // std::cout << "RobinBvh construction time: " << timeSpan.count() << " seconds" << std::endl;
            bvh->printStats();
        }

        return bvh;
    }

    return nullptr;
}

} // namespace zombie
