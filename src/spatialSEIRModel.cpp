#include <Rcpp.h>
#include <cmath>
#include <spatialSEIRModel.hpp>
#include <dataModel.hpp>
#include <exposureModel.hpp>
#include <reinfectionModel.hpp>
#include <distanceModel.hpp>
#include <transitionPriors.hpp>
#include <initialValueContainer.hpp>
#include <samplingControl.hpp>
#include <SEIRSimNodes.hpp>
#include "caf/all.hpp"

using namespace Rcpp;
using namespace caf;

Rcpp::IntegerMatrix createRcppIntFromEigen(Eigen::MatrixXi inMatrix)
{
    Rcpp::IntegerMatrix outMatrix(inMatrix.rows(), inMatrix.cols());
    int i, j;
    for (i = 0; i < inMatrix.cols(); i++)
    {
        for (j = 0; j < inMatrix.rows(); j++)
        {
            outMatrix(j,i) = inMatrix(j,i);
        }
    }
    return(outMatrix);
}

Rcpp::NumericMatrix createRcppNumericFromEigen(Eigen::MatrixXd inMatrix)
{
    Rcpp::NumericMatrix outMatrix(inMatrix.rows(), inMatrix.cols());
    int i, j;
    for (i = 0; i < inMatrix.cols(); i++)
    {
        for (j = 0; j < inMatrix.rows(); j++)
        {
            outMatrix(j,i) = inMatrix(j,i);
        }
    }
    return(outMatrix);
}



spatialSEIRModel::spatialSEIRModel(dataModel& dataModel_,
                                   exposureModel& exposureModel_,
                                   reinfectionModel& reinfectionModel_,
                                   distanceModel& distanceModel_,
                                   transitionPriors& transitionPriors_,
                                   initialValueContainer& initialValueContainer_,
                                   samplingControl& samplingControl_)
{
    // Make sure these pointers go to the real deal
    int err = (((dataModel_.getModelComponentType()) != LSS_DATA_MODEL_TYPE) ||
            ((exposureModel_.getModelComponentType()) != LSS_EXPOSURE_MODEL_TYPE) ||
            ((reinfectionModel_.getModelComponentType()) != LSS_REINFECTION_MODEL_TYPE) ||
            ((distanceModel_.getModelComponentType()) != LSS_DISTANCE_MODEL_TYPE) ||
            ((transitionPriors_.getModelComponentType()) != LSS_TRANSITION_MODEL_TYPE) ||
            ((initialValueContainer_.getModelComponentType()) != LSS_INIT_CONTAINER_TYPE) ||
            ((samplingControl_.getModelComponentType()) != LSS_SAMPLING_CONTROL_MODEL_TYPE));

    if (err != 0)
    { 
        ::Rf_error("Error: model components were not provided in the correct order. \n");
    }

    ncalls = 0;


    int i;
    dataModelInstance = &dataModel_;
    exposureModelInstance = &exposureModel_;
    reinfectionModelInstance = &reinfectionModel_;
    distanceModelInstance = &distanceModel_;
    transitionPriorsInstance = &transitionPriors_;
    initialValueContainerInstance = &initialValueContainer_;
    samplingControlInstance = &samplingControl_;

    dataModelInstance -> protect();
    exposureModelInstance -> protect();
    reinfectionModelInstance -> protect();
    distanceModelInstance -> protect();
    transitionPriorsInstance -> protect();
    initialValueContainerInstance -> protect();
    samplingControlInstance -> protect();

    if ((dataModelInstance -> nLoc) != (exposureModelInstance -> nLoc))
    { 
        ::Rf_error(("Exposure model and data model imply different number of locations: " 
                + std::to_string(dataModelInstance -> nLoc) + ", " 
                + std::to_string(exposureModelInstance -> nLoc) + ".\n").c_str());
    }
    if ((dataModelInstance -> nTpt) != (exposureModelInstance -> nTpt))
    { 
        ::Rf_error(("Exposure model and data model imply different number of time points:"
                    + std::to_string(dataModelInstance -> nTpt) + ", "
                    + std::to_string(exposureModelInstance -> nTpt) + ".\n").c_str());  
    }
    if ((dataModelInstance -> nLoc) != (distanceModelInstance -> numLocations))
    {       
        ::Rf_error(("Data model and distance model imply different number of locations:"
                    + std::to_string(dataModelInstance -> nLoc) + ", "
                    + std::to_string(distanceModelInstance -> numLocations) + ".\n").c_str()
                );
    }
    if ((dataModelInstance -> nLoc) != (initialValueContainerInstance -> S0.size())) 
    { 
        ::Rf_error("Data model and initial value container have different dimensions\n");
    }
    if ((reinfectionModelInstance -> reinfectionMode) == 3)
    {
        // No reinfection
    }
    else
    {
        if (((reinfectionModelInstance -> X_rs).rows()) != (dataModelInstance -> nTpt))
        { 
            ::Rf_error("Reinfection and data mode time points differ.\n");
        }
    }
    if ((reinfectionModelInstance -> reinfectionMode) > 2)
    {
        // pass
    }

}

Rcpp::List spatialSEIRModel::simulate(SEXP inParams)
{
    ncalls += 1;
    Rcpp::NumericMatrix params(inParams);
    self = new scoped_actor();
    // Copy to Eigen matrix
    unsigned int i, j;
    Eigen::MatrixXd param_matrix(params.nrow(), params.ncol());
    for (i = 0; i < params.nrow(); i++)
    {
        for (j = 0; j < params.ncol(); j++)
        {
            param_matrix(i,j) = params(i,j);
        }
    }
    
    std::vector<caf::actor> workers;

    auto worker_pool = actor_pool::make(actor_pool::round_robin{});
    unsigned int ncore = (unsigned int) samplingControlInstance -> CPU_cores;
    unsigned int nrow =  (unsigned int) params.nrow();

    
    for (i = 0; i < ncore; i++)
    {
        workers.push_back((*self) -> spawn<SEIR_sim_node, monitored>(samplingControlInstance->simulation_width,
                                                                      samplingControlInstance->random_seed + 1000*i + ncalls,
                                                                      initialValueContainerInstance -> S0,
                                                                      initialValueContainerInstance -> E0,
                                                                      initialValueContainerInstance -> I0,
                                                                      initialValueContainerInstance -> R0,
                                                                      exposureModelInstance -> offset,
                                                                      dataModelInstance -> Y,
                                                                      distanceModelInstance -> dm_list,
                                                                      exposureModelInstance -> X,
                                                                      reinfectionModelInstance -> X_rs,
                                                                      transitionPriorsInstance -> gamma_ei_params,
                                                                      transitionPriorsInstance -> gamma_ir_params,
                                                                      distanceModelInstance -> spatial_prior,
                                                                      exposureModelInstance -> betaPriorPrecision,
                                                                      reinfectionModelInstance -> betaPriorPrecision, 
                                                                      exposureModelInstance -> betaPriorMean,
                                                                      reinfectionModelInstance -> betaPriorMean,
                                                                      dataModelInstance -> phi,
                                                                      (*self)));
        (*self) -> send(worker_pool, sys_atom::value, put_atom::value, workers[workers.size()-1]);
    }

    // Distribute jobs among workers
    unsigned int outIdx;
    Eigen::VectorXd outRow;

    for (i = 0; i < nrow; i++)
    {
        unsigned int outIdx = i;
        outRow = param_matrix.row(i);
        (*self) -> send(worker_pool, sim_result_atom::value, outIdx, outRow); 
    }

    //std::chrono::milliseconds timespan(1000);                       
    //Rcpp::Rcout << "All data sent to workers\n";
    //Rcpp::Rcout << "dbg_2_a\n"; std::this_thread::sleep_for(timespan); 

    std::vector<int> result_idx;
    //Rcpp::Rcout << "dbg_2_b\n"; std::this_thread::sleep_for(timespan); 
    std::vector<simulationResultSet> results;
    //Rcpp::Rcout << "dbg_2_c\n"; std::this_thread::sleep_for(timespan); 


    i = 0;
    (*self)->receive_for(i, nrow)(
                     [&](unsigned int idx, simulationResultSet result) {
                          results.push_back(result);
                          result_idx.push_back(idx);
                        });

    (*self) -> send_exit(worker_pool, exit_reason::user_shutdown); 

    delete self;
    //shutdown();
    
    Rcpp::List outList;
    for (i = 0; i < nrow; i++)
    {
        Rcpp::List subList;
        subList["S"] = createRcppIntFromEigen(results[i].S);
        subList["E"] = createRcppIntFromEigen(results[i].E);
        subList["I"] = createRcppIntFromEigen(results[i].I);
        subList["R"] = createRcppIntFromEigen(results[i].R);

        subList["S_star"] = createRcppIntFromEigen(results[i].S_star);
        subList["E_star"] = createRcppIntFromEigen(results[i].E_star);
        subList["I_star"] = createRcppIntFromEigen(results[i].I_star);
        subList["R_star"] = createRcppIntFromEigen(results[i].R_star);
        subList["p_se"] = createRcppNumericFromEigen(results[i].p_se);
        subList["p_ei"] = createRcppNumericFromEigen(results[i].p_ei);
        subList["p_ir"] = createRcppNumericFromEigen(results[i].p_ir);
        subList["rho"] = createRcppNumericFromEigen(results[i].rho);
        subList["beta"] = createRcppNumericFromEigen(results[i].beta);
        subList["X"] = createRcppNumericFromEigen(results[i].X);

        subList["result"] = results[i].result;

        outList[std::to_string(i)] = subList;
    }
    return(outList);
}



Rcpp::NumericVector spatialSEIRModel::marginalPosteriorEstimates(SEXP inParams)
{
    ncalls += 1;
    Rcpp::NumericMatrix params(inParams);
    self = new scoped_actor();
    // Copy to Eigen matrix
    unsigned int i, j;
    Eigen::MatrixXd param_matrix(params.nrow(), params.ncol());
    for (i = 0; i < params.nrow(); i++)
    {
        for (j = 0; j < params.ncol(); j++)
        {
            param_matrix(i,j) = params(i,j);
        }
    }
   
    std::vector<caf::actor> workers;

    auto worker_pool = actor_pool::make(actor_pool::round_robin{});
    unsigned int ncore = (unsigned int) samplingControlInstance -> CPU_cores;
    unsigned int nrow =  (unsigned int) params.nrow();

    for (i = 0; i < ncore; i++)
    {
        workers.push_back((*self) -> spawn<SEIR_sim_node, monitored>(samplingControlInstance->simulation_width,
                                                                      samplingControlInstance->random_seed + 1000*i + ncalls,
                                                                      initialValueContainerInstance -> S0,
                                                                      initialValueContainerInstance -> E0,
                                                                      initialValueContainerInstance -> I0,
                                                                      initialValueContainerInstance -> R0,
                                                                      exposureModelInstance -> offset,
                                                                      dataModelInstance -> Y,
                                                                      distanceModelInstance -> dm_list,
                                                                      exposureModelInstance -> X,
                                                                      reinfectionModelInstance -> X_rs,
                                                                      transitionPriorsInstance -> gamma_ei_params,
                                                                      transitionPriorsInstance -> gamma_ir_params,
                                                                      distanceModelInstance -> spatial_prior,
                                                                      exposureModelInstance -> betaPriorPrecision,
                                                                      reinfectionModelInstance -> betaPriorPrecision, 
                                                                      exposureModelInstance -> betaPriorMean,
                                                                      reinfectionModelInstance -> betaPriorMean,
                                                                      dataModelInstance -> phi,
                                                                      (*self)));
/*
        workers.push_back((*self) -> spawn<SEIR_sim_node, monitored>(tmp1,
                                                                      tmp2 + 1000*i + ncalls,
                                                                      tmp3,
                                                                      tmp4,
                                                                      tmp5,
                                                                      tmp6,
                                                                      tmp7,
                                                                      tmp8,
                                                                      tmp9,
                                                                      tmp10,
                                                                      tmp11,
                                                                      tmp12,
                                                                      tmp13,
                                                                      tmp13_1,
                                                                      tmp14,
                                                                      tmp15,
                                                                      tmp16,
                                                                      tmp17,
                                                                      tmp18,
                                                                      (*self)));
*/
        (*self) -> send(worker_pool, sys_atom::value, put_atom::value, workers[workers.size()-1]); 
    }

    // Distribute jobs among workers
    unsigned int outIdx;
    Eigen::VectorXd outRow(param_matrix.cols());
    for (i = 0; i < param_matrix.rows(); i++)
    {
        outIdx = i;
        outRow = param_matrix.row(i);
        (*self) -> send(worker_pool, sim_atom::value, outIdx, outRow); 
    }

    std::vector<int> result_idx;
    std::vector<double> results;

    i = 0;
    (*self)->receive_for(i, nrow)(
                     [&](unsigned int idx, double result) {
                          results.push_back(result);
                          result_idx.push_back(idx);
                        }
                        
                        
                        );


    (*self) -> send_exit(worker_pool, exit_reason::user_shutdown);
    Rcpp::NumericVector out(nrow); 


    for (i = 0; i < nrow; i++)
    {
        out(result_idx[i]) = results[i];
    }

    delete self;
    //shutdown();
    return(out);
}

spatialSEIRModel::~spatialSEIRModel()
{   
    shutdown();
    dataModelInstance -> unprotect();
    exposureModelInstance -> unprotect();
    reinfectionModelInstance -> unprotect();
    distanceModelInstance -> unprotect();
    transitionPriorsInstance -> unprotect();
    initialValueContainerInstance -> unprotect();
    samplingControlInstance -> unprotect();
}


RCPP_MODULE(mod_spatialSEIRModel)
{
    using namespace Rcpp;
    class_<spatialSEIRModel>( "spatialSEIRModel" )
    .constructor<dataModel&,
                 exposureModel&,
                 reinfectionModel&,
                 distanceModel&,
                 transitionPriors&,
                 initialValueContainer&,
                 samplingControl&>()
    .method("marginalPosteriorEstimates", &spatialSEIRModel::marginalPosteriorEstimates)
    .method("simulate", &spatialSEIRModel::simulate);


}

