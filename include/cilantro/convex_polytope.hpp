#pragma once

#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetSet.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertexSet.h>
#include <eigen_quadprog/eiquadprog.hpp>
#include <cilantro/principal_component_analysis.hpp>

template <typename ScalarT, ptrdiff_t EigenDim>
bool checkLinearInequalityConstraintRedundancy(const Eigen::Matrix<ScalarT,EigenDim+1,1> &ineq_to_test,
                                               const Eigen::Ref<const Eigen::Matrix<ScalarT,EigenDim+1,Eigen::Dynamic> > &inequalities,
                                               const Eigen::Matrix<ScalarT,EigenDim,1> &feasible_point,
                                               double dist_tol = std::numeric_limits<double>::epsilon())
{
    size_t num_inequalities = inequalities.cols();
    if (num_inequalities == 0) return false;
    Eigen::MatrixXd ineq_data(inequalities.template cast<double>());
    Eigen::VectorXd ineq_test(ineq_to_test.template cast<double>());

    // Normalize input
    // Force unit length normals
    for (size_t i = 0; i < num_inequalities; i++) {
        ineq_data.col(i) /= ineq_data.col(i).head(EigenDim).norm();
    }
    ineq_test /= ineq_test.head(EigenDim).norm();

    // Center halfspaces around origin and then scale dimensions
    Eigen::VectorXd t_vec(-feasible_point.template cast<double>());
    ineq_data.row(EigenDim) = ineq_data.row(EigenDim) - t_vec.transpose()*ineq_data.topRows(EigenDim);
    ineq_test(EigenDim) = ineq_test(EigenDim) - t_vec.dot(ineq_test.head(EigenDim));
    double max_abs_dist = ineq_data.row(EigenDim).cwiseAbs().maxCoeff();
    double scale = (max_abs_dist < dist_tol) ? 1.0 : 1.0/max_abs_dist;
    ineq_data.row(EigenDim) *= scale;
    ineq_test(EigenDim) *= scale;

    // Objective
    // 'Preconditioned' quadratic term
    ScalarT tol_sq = dist_tol*dist_tol;
    Eigen::MatrixXd G(ineq_test*(ineq_test.transpose()));
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(G, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::VectorXd S(svd.singularValues());
    for (size_t i = 0; i < S.size(); i++) {
        if (S(i) < tol_sq) S(i) = tol_sq;
    }
    G = svd.matrixU()*(S.asDiagonal())*(svd.matrixV().transpose());

    // Linear term
    Eigen::VectorXd g0(-ineq_test);
    g0(EigenDim) = 0.0;

    // Equality constraints
    Eigen::MatrixXd CE(Eigen::VectorXd::Zero(EigenDim+1));
    CE(EigenDim) = 1.0;
    Eigen::VectorXd ce0(1);
    ce0(0) = -1.0;

    // Inequality constraints
    Eigen::MatrixXd CI(-ineq_data);
    Eigen::VectorXd ci0(Eigen::VectorXd::Zero(num_inequalities));

    // Optimization
    Eigen::VectorXd x(EigenDim+1);
    double val = solve_quadprog(G, g0, CE, ce0, CI, ci0, x);

//    Eigen::VectorXd sol((x.head(EigenDim)/scale - t_vec));
//    std::cout << val << ", for: " << x.transpose() << " (" << sol.transpose() << ")" << std::endl;
//    Eigen::VectorXd ineq_test0(ineq_to_test.template cast<double>());
//    std::cout << "dist: " << (sol.dot(ineq_test0.head(EigenDim)) + ineq_test0(EigenDim)) << std::endl;

    if (std::isinf(val) || std::isnan(val) || x.array().isNaN().any() || x.array().isInf().any()) return false;

    return x.head(EigenDim).dot(ineq_test.head(EigenDim)) + ineq_test(EigenDim) < -dist_tol;
}

template <typename ScalarT, ptrdiff_t EigenDim>
inline bool checkLinearInequalityConstraintRedundancy(const Eigen::Matrix<ScalarT,EigenDim+1,1> &ineq_to_test,
                                                      const std::vector<Eigen::Matrix<ScalarT,EigenDim+1,1> > &inequalities,
                                                      const Eigen::Matrix<ScalarT,EigenDim,1> &feasible_point,
                                                      double dist_tol = std::numeric_limits<double>::epsilon())
{
    return checkLinearInequalityConstraintRedundancy<ScalarT,EigenDim>(ineq_to_test, Eigen::Map<Eigen::Matrix<ScalarT,EigenDim+1,Eigen::Dynamic> >((ScalarT *)inequalities.data(),EigenDim+1,inequalities.size()), feasible_point, dist_tol);
}

template <typename ScalarT, ptrdiff_t EigenDim>
bool findFeasiblePointInHalfspaceIntersection(const Eigen::Ref<const Eigen::Matrix<ScalarT,EigenDim+1,Eigen::Dynamic> > &halfspaces,
                                              Eigen::Matrix<ScalarT,EigenDim,1> &feasible_point,
                                              double dist_tol = std::numeric_limits<double>::epsilon(),
                                              bool force_strictly_interior = true)
{
    size_t num_halfspaces = halfspaces.cols();
    if (num_halfspaces == 0) {
        feasible_point.setZero();
        return true;
    }
    Eigen::MatrixXd ineq_data(halfspaces.template cast<double>());

    // Normalize input
    // Force unit length normals
    for (size_t i = 0; i < num_halfspaces; i++) {
        ineq_data.col(i) /= ineq_data.col(i).head(EigenDim).norm();
    }

    // Center halfspaces around origin and then scale dimensions
    Eigen::Matrix<double,EigenDim,1> t_vec((ineq_data.topRows(EigenDim).array().rowwise()*ineq_data.row(EigenDim).array().abs()).rowwise().mean());
    ineq_data.row(EigenDim) = ineq_data.row(EigenDim) - t_vec.transpose()*ineq_data.topRows(EigenDim);
    double max_abs_dist = ineq_data.row(EigenDim).cwiseAbs().maxCoeff();
    double scale = (max_abs_dist < dist_tol) ? 1.0 : 1.0/max_abs_dist;
    ineq_data.row(EigenDim) *= scale;

    // Objective
    // 'Preconditioned' quadratic term
    ScalarT tol_sq = dist_tol*dist_tol;
    Eigen::MatrixXd G(EigenDim+2,EigenDim+2);
    G.topLeftCorner(EigenDim+1,EigenDim+1) = ineq_data*(ineq_data.transpose());
    G.row(EigenDim+1).setZero();
    G.col(EigenDim+1).setZero();
    G(EigenDim+1,EigenDim+1) = 1.0;
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(G, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::VectorXd S(svd.singularValues());
    for (size_t i = 0; i < S.size(); i++) {
        if (S(i) < tol_sq) S(i) = tol_sq;
    }
    G = svd.matrixU()*(S.asDiagonal())*(svd.matrixV().transpose());

    // Linear term
    Eigen::VectorXd g0(Eigen::VectorXd::Zero(EigenDim+2));
    g0(EigenDim+1) = -1.0;

    // Equality constraints
    Eigen::MatrixXd CE(Eigen::VectorXd::Zero(EigenDim+2));
    CE(EigenDim) = 1.0;
    Eigen::VectorXd ce0(1);
    ce0(0) = -1.0;

    // Inequality constraints
    Eigen::MatrixXd CI(EigenDim+2,num_halfspaces+1);
    CI.topLeftCorner(EigenDim+1,num_halfspaces) = -ineq_data;
    CI.row(EigenDim+1).setConstant(-1.0);
    CI.col(num_halfspaces).setZero();
    CI(EigenDim+1,num_halfspaces) = 1.0;
    Eigen::VectorXd ci0(Eigen::VectorXd::Zero(num_halfspaces+1));

    // Optimization
    Eigen::VectorXd x(EigenDim+2);
    double val = solve_quadprog(G, g0, CE, ce0, CI, ci0, x);
    Eigen::Matrix<double,EigenDim,1> fp(x.head(EigenDim));
    feasible_point = (fp/scale - t_vec).template cast<ScalarT>();

    //std::cout << val << ", for: " << x.transpose() << "   (" << feasible_point.transpose() << ")" << std::endl;

    if (std::isinf(val) || std::isnan(val) || x.array().isNaN().any() || x.array().isInf().any()) return false;

    // Useful in case of unbounded intersections
    if (force_strictly_interior && x(EigenDim+1) < dist_tol) {
        size_t num_additional = 0;
        std::vector<size_t> tight_ind(num_halfspaces);
        for (size_t i = 0; i < num_halfspaces; i++) {
            if (std::abs(ineq_data.col(i).dot(x.head(EigenDim+1))) < dist_tol) {
                tight_ind[num_additional++] = i;
            }
        }
        tight_ind.resize(num_additional);

        if (num_additional > 0) {
            double offset = (double)(num_halfspaces - 1);
            Eigen::Matrix<double,EigenDim+1,Eigen::Dynamic> halfspaces_tight(EigenDim+1,num_halfspaces+num_additional);
            halfspaces_tight.leftCols(num_halfspaces) = ineq_data;
            num_additional = num_halfspaces;
            for (size_t i = 0; i < tight_ind.size(); i++) {
                halfspaces_tight.col(num_additional) = -ineq_data.col(tight_ind[i]);
                halfspaces_tight(EigenDim,num_additional) -= offset;
                num_additional++;
            }

            bool res = findFeasiblePointInHalfspaceIntersection<double,EigenDim>(halfspaces_tight, fp, dist_tol, false);
            feasible_point = (fp/scale - t_vec).template cast<ScalarT>();
            return res;
        }
    }

    return num_halfspaces > 0;
}

template <typename ScalarT, ptrdiff_t EigenDim>
inline bool findFeasiblePointInHalfspaceIntersection(const std::vector<Eigen::Matrix<ScalarT,EigenDim+1,1> > &halfspaces,
                                                     Eigen::Matrix<ScalarT,EigenDim,1> &feasible_point,
                                                     double dist_tol = std::numeric_limits<double>::epsilon(),
                                                     bool force_strictly_interior = true)
{
    return findFeasiblePointInHalfspaceIntersection<ScalarT,EigenDim>(Eigen::Map<Eigen::Matrix<ScalarT,EigenDim+1,Eigen::Dynamic> >((ScalarT *)halfspaces.data(),EigenDim+1,halfspaces.size()), feasible_point, dist_tol, force_strictly_interior);
}


template <typename InputScalarT, typename OutputScalarT, ptrdiff_t EigenDim>
bool evaluateHalfspaceIntersection(const Eigen::Ref<const Eigen::Matrix<InputScalarT,EigenDim+1,Eigen::Dynamic> > &halfspaces,
                                   std::vector<Eigen::Matrix<OutputScalarT,EigenDim+1,1> > &facet_halfspaces,
                                   std::vector<Eigen::Matrix<OutputScalarT,EigenDim,1> > &polytope_vertices,
                                   Eigen::Matrix<OutputScalarT,EigenDim,1> &interior_point,
                                   bool &is_bounded,
                                   double dist_tol = std::numeric_limits<double>::epsilon(),
                                   double merge_tol = 0.0)
{
    // Set up variables
    Eigen::Matrix<double,EigenDim+1,Eigen::Dynamic> hs_coeffs(halfspaces.template cast<double>());
    Eigen::Matrix<double,EigenDim,1> feasible_point;
    size_t num_halfspaces = halfspaces.cols();

    // Force unit length normals
    for (size_t i = 0; i < num_halfspaces; i++) {
        hs_coeffs.col(i) /= hs_coeffs.col(i).head(EigenDim).norm();
    }

    bool is_empty = !findFeasiblePointInHalfspaceIntersection<double,EigenDim>(hs_coeffs, feasible_point, dist_tol, true);
    interior_point = feasible_point.template cast<OutputScalarT>();

    // Check if intersection is empty
    if (is_empty) {
        facet_halfspaces.resize(2);
        facet_halfspaces[0].setZero();
        facet_halfspaces[0](0) = 1.0;
        facet_halfspaces[0](EigenDim) = 1.0;
        facet_halfspaces[1].setZero();
        facet_halfspaces[1](0) = -1.0;
        facet_halfspaces[1](EigenDim) = 1.0;
        polytope_vertices.clear();
        is_bounded = true;

        return false;
    }

    Eigen::FullPivLU<Eigen::Matrix<double,EigenDim,Eigen::Dynamic> > lu(hs_coeffs.topRows(EigenDim));
    lu.setThreshold(dist_tol);

    //std::cout << "The rank of A is found to be " << lu.rank() << std::endl;

    if (lu.rank() < EigenDim) {
        facet_halfspaces.resize(num_halfspaces);
        size_t nr = 0;
        for (size_t i = 0; i < num_halfspaces; i++) {
            hs_coeffs.col(i).swap(hs_coeffs.col(num_halfspaces-1));
            if (!checkLinearInequalityConstraintRedundancy<double,EigenDim>(hs_coeffs.col(num_halfspaces-1), hs_coeffs.leftCols(num_halfspaces-1), feasible_point, dist_tol)) {
                facet_halfspaces[nr++] = hs_coeffs.col(num_halfspaces-1).template cast<OutputScalarT>();
            }
            hs_coeffs.col(i).swap(hs_coeffs.col(num_halfspaces-1));
        }
        facet_halfspaces.resize(nr);
        polytope_vertices.clear();
        is_bounded = false;

        return true;
    }

    is_bounded = true;

    // 'Precondition' qhull input...
    if (std::abs(hs_coeffs.row(EigenDim).maxCoeff() - hs_coeffs.row(EigenDim).minCoeff()) < dist_tol) {
        hs_coeffs.conservativeResize(Eigen::NoChange, 2*num_halfspaces);
        hs_coeffs.rightCols(num_halfspaces) = hs_coeffs.leftCols(num_halfspaces);
        hs_coeffs.block(EigenDim,num_halfspaces,1,num_halfspaces).array() -= 1.0;
        num_halfspaces *= 2;
    }

    // Run qhull in halfspace mode
    std::vector<double> fpv(EigenDim);
    Eigen::Matrix<double,EigenDim,1>::Map(fpv.data()) = feasible_point;

    orgQhull::Qhull qh;
    qh.qh()->HALFspace = True;
    qh.qh()->PRINTprecision = False;
    //qh.qh()->JOGGLEmax = 0.0;
    qh.qh()->TRIangulate = False;
    qh.qh()->premerge_centrum = merge_tol;
    qh.setFeasiblePoint(orgQhull::Coordinates(fpv));
    qh.runQhull("", EigenDim+1, num_halfspaces, hs_coeffs.data(), "");

    // Get polytope vertices from dual hull facets
    orgQhull::QhullFacetList facets = qh.facetList();
    size_t num_dual_facets = facets.size();

    polytope_vertices.resize(num_dual_facets);
    std::vector<Eigen::Matrix<double,EigenDim,1> > vertices(num_dual_facets);

    size_t ind = 0;
    for (auto fi = facets.begin(); fi != facets.end(); ++fi) {
        if (fi->hyperplane().offset() < 0.0) {
            Eigen::Matrix<double,EigenDim,1> normal;
            size_t i = 0;
            for (auto hpi = fi->hyperplane().begin(); hpi != fi->hyperplane().end(); ++hpi) {
                normal(i++) = *hpi;
            }
            vertices[ind] = feasible_point - normal/fi->hyperplane().offset();
            polytope_vertices[ind] = vertices[ind].template cast<OutputScalarT>();
            ind++;
        } else {
            is_bounded = false;
        }
    }
    vertices.resize(ind);
    polytope_vertices.resize(ind);

    Eigen::Map<Eigen::Matrix<double,EigenDim,Eigen::Dynamic> > v_map((double *)vertices.data(),EigenDim,vertices.size());

    // Get facet hyperplanes from dual hull vertices
    facet_halfspaces.resize(qh.vertexList().size());
    ind = 0;
    for (auto vi = qh.vertexList().begin(); vi != qh.vertexList().end(); ++vi) {
        Eigen::Matrix<double,EigenDim+1,1> hs(hs_coeffs.col(vi->point().id()));
        if (((hs.head(EigenDim).transpose()*v_map).array() + hs(EigenDim)).cwiseAbs().minCoeff() < dist_tol) {
            facet_halfspaces[ind++] = hs.template cast<OutputScalarT>();
        }
    }
    facet_halfspaces.resize(ind);

    return true;
}

template <typename InputScalarT, typename OutputScalarT, ptrdiff_t EigenDim>
inline bool evaluateHalfspaceIntersection(const std::vector<Eigen::Matrix<InputScalarT,EigenDim+1,1> > &halfspaces,
                                          std::vector<Eigen::Matrix<OutputScalarT,EigenDim+1,1> > &facet_halfspaces,
                                          std::vector<Eigen::Matrix<OutputScalarT,EigenDim,1> > &polytope_vertices,
                                          Eigen::Matrix<OutputScalarT,EigenDim,1> &interior_point,
                                          bool &is_bounded,
                                          double dist_tol = std::numeric_limits<double>::epsilon(),
                                          double merge_tol = 0.0)
{
    return evaluateHalfspaceIntersection<InputScalarT,OutputScalarT,EigenDim>(Eigen::Map<Eigen::Matrix<InputScalarT,EigenDim+1,Eigen::Dynamic> >((InputScalarT *)halfspaces.data(),EigenDim+1,halfspaces.size()), facet_halfspaces, polytope_vertices, interior_point, is_bounded, dist_tol, merge_tol);
}

template <typename InputScalarT, typename OutputScalarT, ptrdiff_t EigenDim>
bool halfspaceIntersectionFromVertices(const Eigen::Ref<const Eigen::Matrix<InputScalarT,EigenDim,Eigen::Dynamic> > &vertices,
                                       std::vector<Eigen::Matrix<OutputScalarT,EigenDim,1> > &polytope_vertices,
                                       std::vector<Eigen::Matrix<OutputScalarT,EigenDim+1,1> > &facet_halfspaces,
                                       double &area, double &volume,
                                       double merge_tol = 0.0)
{
    size_t num_points = vertices.cols();
    Eigen::Matrix<double,EigenDim,Eigen::Dynamic> vert_data(vertices.template cast<double>());

    if (num_points == 0) {
        polytope_vertices.clear();
        facet_halfspaces.resize(2);
        facet_halfspaces[0].setZero();
        facet_halfspaces[0](0) = 1.0;
        facet_halfspaces[0](EigenDim) = 1.0;
        facet_halfspaces[1].setZero();
        facet_halfspaces[1](0) = -1.0;
        facet_halfspaces[1](EigenDim) = 1.0;
        area = 0.0;
        volume = 0.0;
        return false;
    }

    Eigen::Matrix<double,EigenDim,1> mu(vert_data.rowwise().mean());
    size_t true_dim = Eigen::FullPivLU<Eigen::Matrix<double,EigenDim,Eigen::Dynamic> >(vert_data.colwise() - mu).rank();

    //std::cout << "TRUE DIMENSION: " << true_dim << std::endl << std::endl;

    if (true_dim < EigenDim) {
        PrincipalComponentAnalysis<double,EigenDim> pca(vert_data);
        const Eigen::Matrix<double,EigenDim,1>& t_vec(pca.getDataMean());
        const Eigen::Matrix<double,EigenDim,EigenDim>& rot_mat(pca.getEigenVectorsMatrix());

        Eigen::MatrixXd proj_vert((rot_mat.transpose()*(vert_data.colwise() - t_vec)).topRows(true_dim));

        Eigen::Matrix<double,EigenDim,Eigen::Dynamic> polytope_vertices_d(EigenDim,0);
        Eigen::Matrix<double,EigenDim+1,Eigen::Dynamic> facet_halfspaces_d(EigenDim+1,0);

        size_t hs_ind = 0;

        // Populate polytope vertices amd add constraints for first true_dim dimensions
        if (true_dim > 1) {
            // Get vertices and constraints from qhull
            orgQhull::Qhull qh;
            qh.qh()->TRIangulate = False;
            qh.qh()->premerge_centrum = merge_tol;
            qh.runQhull("", true_dim, num_points, proj_vert.data(), "");
            qh.defineVertexNeighborFacets();
            orgQhull::QhullFacetList qh_facets = qh.facetList();

            // Populate polytope vertices
            size_t vert_ind = 0;
            polytope_vertices_d = Eigen::MatrixXd::Zero(EigenDim, qh.vertexCount());
            for (auto vi = qh.vertexList().begin(); vi != qh.vertexList().end(); ++vi) {
                size_t i = 0;
                for (auto ci = vi->point().begin(); ci != vi->point().end(); ++ci) {
                    polytope_vertices_d(i++,vert_ind) = *ci;
                }
                vert_ind++;
            }

            // Populate facet halfspaces
            facet_halfspaces_d = Eigen::MatrixXd::Zero(EigenDim+1, qh.facetCount() + 2*(EigenDim - true_dim));
            for (auto fi = qh_facets.begin(); fi != qh_facets.end(); ++fi) {
                size_t i = 0;
                for (auto hpi = fi->hyperplane().begin(); hpi != fi->hyperplane().end(); ++hpi) {
                    facet_halfspaces_d(i++,hs_ind) = *hpi;
                }
                facet_halfspaces_d(EigenDim,hs_ind) = fi->hyperplane().offset();
                hs_ind++;
            }

            area = (EigenDim == true_dim + 1) ? qh.volume() : 0.0;
            volume = 0.0;
        } else if (true_dim == 0) {
            // Handle special case (single point)
            polytope_vertices_d = Eigen::MatrixXd::Zero(EigenDim,1);
            facet_halfspaces_d = Eigen::MatrixXd::Zero(EigenDim+1,2*EigenDim);
            area = 0.0;
            volume = 0.0;
        } else if (true_dim == 1) {
            // Handle special case (1D line, not handled by qhull)
            size_t ind_min, ind_max;
            double min_val = proj_vert.row(0).minCoeff(&ind_min);
            double max_val = proj_vert.row(0).maxCoeff(&ind_max);

            polytope_vertices_d = Eigen::MatrixXd::Zero(EigenDim,2);
            polytope_vertices_d.block(0,0,true_dim,1) = proj_vert.col(ind_min);
            polytope_vertices_d.block(0,1,true_dim,1) = proj_vert.col(ind_max);

            facet_halfspaces_d = Eigen::MatrixXd::Zero(EigenDim+1,2*EigenDim);
            facet_halfspaces_d(0,0) = -1.0;
            facet_halfspaces_d(EigenDim,0) = min_val;
            facet_halfspaces_d(0,1) = 1.0;
            facet_halfspaces_d(EigenDim,1) = -max_val;
            hs_ind = 2;

            area = (EigenDim == 2) ? (max_val-min_val) : 0.0;
            volume = 0.0;
        }

        // Add (equality) constraints for remaining dimensions
        for (size_t dim = true_dim; dim < EigenDim; dim++) {
            facet_halfspaces_d(dim,hs_ind++) = -1.0;
            facet_halfspaces_d(dim,hs_ind++) = 1.0;
        }

        // Project back into output arguments
        polytope_vertices.resize(polytope_vertices_d.cols());
        Eigen::Matrix<OutputScalarT,EigenDim,Eigen::Dynamic>::Map((OutputScalarT *)polytope_vertices.data(),EigenDim,polytope_vertices.size()) = ((rot_mat*polytope_vertices_d).colwise() + t_vec).template cast<OutputScalarT>();

        Eigen::Matrix<double,EigenDim+1,EigenDim+1> tform(Eigen::Matrix<double,EigenDim+1,EigenDim+1>::Zero());
        tform.template block<EigenDim,EigenDim>(0,0) = rot_mat;
        tform.template block<EigenDim,1>(0,EigenDim) = t_vec;
        tform(EigenDim,EigenDim) = 1.0;

        facet_halfspaces_d = tform.inverse().transpose()*facet_halfspaces_d;
//        for (size_t i = 0; i < facet_halfspaces_d.cols(); i++) {
//            facet_halfspaces_d.col(i) /= facet_halfspaces_d.col(i).head(EigenDim).norm();
//        }

        facet_halfspaces.resize(facet_halfspaces_d.cols());
        Eigen::Matrix<OutputScalarT,EigenDim+1,Eigen::Dynamic>::Map((OutputScalarT *)facet_halfspaces.data(),EigenDim+1,facet_halfspaces.size()) = facet_halfspaces_d.template cast<OutputScalarT>();

        return true;
    }

    orgQhull::Qhull qh;
    qh.qh()->TRIangulate = False;
    qh.qh()->premerge_centrum = merge_tol;
    qh.runQhull("", EigenDim, num_points, vert_data.data(), "");
    qh.defineVertexNeighborFacets();
    orgQhull::QhullFacetList qh_facets = qh.facetList();

    // Populate polytope vertices
    size_t k = 0;
    polytope_vertices.resize(qh.vertexCount());
    for (auto vi = qh.vertexList().begin(); vi != qh.vertexList().end(); ++vi) {
        size_t i = 0;
        Eigen::Matrix<double,EigenDim,1> v;
        for (auto ci = vi->point().begin(); ci != vi->point().end(); ++ci) {
            v(i++) = *ci;
        }
        polytope_vertices[k++] = v.template cast<OutputScalarT>();
    }

    // Populate facet halfspaces
    k = 0;
    facet_halfspaces.resize(qh.facetCount());
    for (auto fi = qh_facets.begin(); fi != qh_facets.end(); ++fi) {
        size_t i = 0;
        Eigen::Matrix<double,EigenDim+1,1> hp;
        for (auto hpi = fi->hyperplane().begin(); hpi != fi->hyperplane().end(); ++hpi) {
            hp(i++) = *hpi;
        }
        hp(EigenDim) = fi->hyperplane().offset();
        facet_halfspaces[k++] = hp.template cast<OutputScalarT>();
    }

    area = qh.area();
    volume = qh.volume();

    return true;
}

//template <typename InputScalarT, typename OutputScalarT, ptrdiff_t EigenDim>
//bool halfspaceIntersectionFromVertices(const Eigen::Ref<const Eigen::Matrix<InputScalarT,EigenDim,Eigen::Dynamic> > &vertices,
//                                       std::vector<Eigen::Matrix<OutputScalarT,EigenDim,1> > &polytope_vertices,
//                                       std::vector<Eigen::Matrix<OutputScalarT,EigenDim+1,1> > &facet_halfspaces,
//                                       double &area, double &volume,
//                                       double merge_tol = 0.0)
//{
//    size_t num_points = vertices.cols();
//    Eigen::Matrix<double,EigenDim,Eigen::Dynamic> vert_data(vertices.template cast<double>());
//
//    orgQhull::Qhull qh;
//    qh.qh()->TRIangulate = False;
//    qh.qh()->premerge_centrum = merge_tol;
//    qh.runQhull("", EigenDim, num_points, vert_data.data(), "");
//    qh.defineVertexNeighborFacets();
//    orgQhull::QhullFacetList qh_facets = qh.facetList();
//
//    // Populate polytope vertices
//    size_t k = 0;
//    polytope_vertices.resize(qh.vertexCount());
//    for (auto vi = qh.vertexList().begin(); vi != qh.vertexList().end(); ++vi) {
//        size_t i = 0;
//        Eigen::Matrix<double,EigenDim,1> v;
//        for (auto ci = vi->point().begin(); ci != vi->point().end(); ++ci) {
//            v(i++) = *ci;
//        }
//        polytope_vertices[k++] = v.template cast<OutputScalarT>();
//    }
//
//    // Populate facet halfspaces
//    k = 0;
//    facet_halfspaces.resize(qh_facets.size());
//    for (auto fi = qh_facets.begin(); fi != qh_facets.end(); ++fi) {
//        size_t i = 0;
//        Eigen::Matrix<double,EigenDim+1,1> hp;
//        for (auto hpi = fi->hyperplane().begin(); hpi != fi->hyperplane().end(); ++hpi) {
//            hp(i++) = *hpi;
//        }
//        hp(EigenDim) = fi->hyperplane().offset();
//        facet_halfspaces[k++] = hp.template cast<OutputScalarT>();
//    }
//
//    area = qh.area();
//    volume = qh.volume();
//
//    return true;
//}

template <typename InputScalarT, typename OutputScalarT, ptrdiff_t EigenDim>
inline bool halfspaceIntersectionFromVertices(const std::vector<Eigen::Matrix<InputScalarT,EigenDim,1> > &vertices,
                                              std::vector<Eigen::Matrix<OutputScalarT,EigenDim,1> > &polytope_vertices,
                                              std::vector<Eigen::Matrix<OutputScalarT,EigenDim+1,1> > &facet_halfspaces,
                                              double &area, double &volume,
                                              double merge_tol = 0.0)
{
    return halfspaceIntersectionFromVertices<InputScalarT,OutputScalarT,EigenDim>(Eigen::Map<Eigen::Matrix<InputScalarT,EigenDim,Eigen::Dynamic> >((InputScalarT *)vertices.data(),EigenDim,vertices.size()), polytope_vertices, facet_halfspaces, area, volume, merge_tol);
}
