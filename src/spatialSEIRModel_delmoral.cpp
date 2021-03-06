#include <Rcpp.h>
#include <Eigen/Core>
#include <RcppEigen.h>
#include <cmath>
#include <math.h>
#include <spatialSEIRModel.hpp>
#include <dataModel.hpp>
#include <exposureModel.hpp>
#include <reinfectionModel.hpp>
#include <distanceModel.hpp>
#include <transitionPriors.hpp>
#include <initialValueContainer.hpp>
#include <samplingControl.hpp>
#include <util.hpp>
#include <SEIRSimNodes.hpp>

void printMaxMin(Eigen::MatrixXd in)
{
    double maxMin = -1.0;
    double overallMax = -1.0;
    double subMin = std::numeric_limits<double>::infinity();
    double overallMin = std::numeric_limits<double>::infinity();
    int nNan = 0;


    int i,j;
    for (i = 0; i < in.rows(); i++)
    {
        subMin = std::numeric_limits<double>::infinity();
        for (j = 0; j < in.cols(); j++)
        {
            if (in(i,j) < subMin) subMin = in(i,j);
            if (in(i,j) < overallMin) overallMin = in(i,j);
            if (in(i,j) > overallMax) overallMax = in(i,j);
            if (std::isnan(in(i,j))){
                nNan ++;
            }

        }
        if (subMin > maxMin) maxMin = subMin;
    }
    Rcpp::Rcout << "Max-min over m: " << maxMin << "\n";
    Rcpp::Rcout << "Overall max:" << overallMax << "\n";
    Rcpp::Rcout << "Overall min:" << overallMin << "\n";
    Rcpp::Rcout << "Num NAN:" << nNan << "\n";

}

// [[Rcpp::export]]
Eigen::VectorXd calculate_weights_DM(double cur_e,
                                  double prev_e, 
                                  Eigen::MatrixXd eps,
                                  Eigen::VectorXd prev_wts)
{
    Eigen::VectorXd out_wts = Eigen::VectorXd::Zero(eps.rows());
    int i, j;
    int M = eps.cols();
    double num, denom, tot;
    tot = 0.0;
    for (i = 0; i < eps.rows(); i++)
    {
        num = 0.0;
        denom = 0.0;
        for (j = 0; j < M; j++)
        {
            // Should have two epsilons here?
            num += eps(i,j) < cur_e;
            denom += eps(i,j) < prev_e;
        } 
        out_wts(i) = (num/denom*prev_wts(i));
        tot += out_wts(i);
    }
    if (!std::isfinite(tot))
    {
        Rcpp::Rcout << "non-finite weights encountered, rerunning calculation with debug info.\n";
        Rcpp::Rcout << "Calculating weights at " << cur_e << " vs. " << prev_e << "\n";
        tot = 0.0;

        for (i = 0; i < eps.rows(); i++)
        {
            Rcpp::Rcout << "i = " << i << "\n";
            num = 0.0;
            denom = 0.0;
            for (j = 0; j < M; j++)
            {
                num += eps(i,j) < cur_e;
                denom += eps(i,j) < prev_e;
                Rcpp::Rcout << "eps(i,j) = " << eps(i,j) << "\n";
                Rcpp::Rcout << " (n/d) = (" << num << "/" << denom << ")\n";
            } 
            out_wts(i) = (num/denom*prev_wts(i));
            Rcpp::Rcout << "out_wts(" << i << ") = " << out_wts(i) << "\n"; 
            tot += out_wts(i);
            Rcpp::Rcout << "tot = " << tot << "\n";
        }

        Rcpp::stop("non-finite weights encountered.");
    }
    out_wts.array() /= tot;
    return(out_wts);
}

double ESS(Eigen::VectorXd wts)
{
    double out = 0.0;
    for (int i = 0; i < wts.size(); i++)
    {
        out += std::pow(wts(i), 2.0);
    }
    return(1.0/out); 
}

double eps_f(double rhs,
             double cur_e,
             double prev_e, 
             Eigen::MatrixXd eps,
             Eigen::VectorXd prev_wts)
{
    return(std::pow(rhs - ESS(calculate_weights_DM(cur_e, 
                                                prev_e, 
                                                eps, 
                                                prev_wts)), 2.0));
}

// [[Rcpp::export]]
double solve_for_epsilon(double LB,
                       double UB,
                       double prev_e,
                       double alpha,
                       Eigen::MatrixXd eps,
                       Eigen::VectorXd prev_wts)
{
    double phi = (1.0 + std::sqrt(5))/2.0; 
    double a,b,c,d,fc,fd;
    //double a,b,c,d,fa,fb,fc,fd;

    //double proposed_e = proposed_e; 
    /*double rhs = ESS(calculate_weights_DM(proposed_e,
                                         prev_e, 
                                         eps,
                                         prev_wts))*alpha;*/
    double rhs = ESS(prev_wts)*alpha;

    a = LB;
    c = LB;
    b = UB;
    d = UB;
    //fa = eps_f(rhs, a, prev_e, eps, prev_wts);
    //fb = eps_f(rhs, b, prev_e, eps, prev_wts);
    bool mvLB = true;
    bool mvUB = true;
    int itrs = 0;
    double diff = 100.0;
    while (itrs < 10000 && diff > 0.5)
    {
       if (mvLB)
       {
           c = b + (a - b)/phi; 
           mvLB = false;
       }
       if (mvUB)
       {
           d = a + (b - a)/phi;
           mvUB = false;
       }
       fc = eps_f(rhs, c, prev_e, eps, prev_wts);
       fd = eps_f(rhs, d, prev_e, eps, prev_wts);

       if (fc < fd)
       {
         b = d;
         //fb = fd;
         d = c;
         fd = fc;
         mvLB = true;
       }
       else
       {
         a = c;
         //fa = fc;
         c = d;
         fc = fd;
         mvUB = true;
       }
       diff = b-a;
       itrs++;
    }
    /*
    if (diff > 1e-4)
    {
       Rcpp::Rcout << "Warning: optimization didn't converge.\n";
       Rcpp::Rcout << "Diff: " << diff << "\n";
    }
    */
    return((a+b)/2.0);

}

void proposeParams(Eigen::MatrixXd* params,
                   Eigen::VectorXd* tau,
                   std::mt19937* generator)
{
    int i,j;
    // Propose new parameters
    int p = params -> cols();
    int N = params -> rows();
    for (j = 0; j < p; j++)
    {
        auto propDist = std::normal_distribution<double>(0.0, 2.0*(*tau)(j));
        for (i = 0; i < N; i++)
        {
            (*params)(i,j) += propDist(*generator);
        }
    }
}

Rcpp::List spatialSEIRModel::sample_DelMoral2012(int nSample, int vb, 
                                                 std::string sim_type_atom)
{
    // This is set by constructor
    const int nParams = param_matrix.cols();

    // Accepted params/results are nSample size
    results_complete = std::vector<simulationResultSet>();
    results_double = Eigen::MatrixXd::Zero(nSample, 
                                           samplingControlInstance -> m); 
    param_matrix = Eigen::MatrixXd::Zero(nSample, 
                                            nParams);
    prev_param_matrix = param_matrix;
    // Proposals matrices are batch size
    proposed_results_double = Eigen::MatrixXd::Zero(samplingControlInstance -> batch_size, 
                                               samplingControlInstance -> m); 
    proposed_param_matrix = Eigen::MatrixXd::Zero(samplingControlInstance -> batch_size, 
                                            nParams);
    proposal_cache = Eigen::MatrixXd::Zero(samplingControlInstance -> batch_size, 
                                            nParams);
    preproposal_params = Eigen::MatrixXd::Zero(samplingControlInstance -> batch_size, 
                                            nParams);
    preproposal_results = Eigen::MatrixXd::Zero(samplingControlInstance -> batch_size, 
                                               samplingControlInstance -> m); 
 
    const int verbose = vb;
    if (verbose > 1)
    {
        Rcpp::Rcout << "Starting sampler\n";
    }
    const int num_iterations = samplingControlInstance -> epochs;
    const int Nsim = samplingControlInstance -> batch_size;
    const int Npart = nSample;
    if (Npart != Nsim)
    {
        Rcpp::stop("Disparate simulation and particle size temporarily disabled\n");
    }
    const int maxBatches= samplingControlInstance -> max_batches;
    const bool hasReinfection = (reinfectionModelInstance -> 
            betaPriorPrecision)(0) > 0;
    const bool hasSpatial = (dataModelInstance -> Y).cols() > 1;

    std::string transitionMode = transitionPriorsInstance -> mode;   

    double e0 = std::numeric_limits<double>::infinity();
    double e1 = std::numeric_limits<double>::infinity();

    int i,j;
    int iteration;

    auto U = std::uniform_real_distribution<double>(0,1);
    double drw;

    if (verbose > 1)
    {
        Rcpp::Rcout << "Number of iterations requested: " 
                    << num_iterations << "\n";

        dataModelInstance -> summary();
        exposureModelInstance -> summary();
        reinfectionModelInstance -> summary();
        distanceModelInstance -> summary();
        initialValueContainerInstance -> summary();
        samplingControlInstance -> summary();

    }
    
    if (!is_initialized)
    {
        if (verbose > 1){Rcpp::Rcout << "Generating starting parameters from prior\n";}
        // Sample parameters from their prior
        param_matrix = generateParamsPrior(Npart);
        run_simulations(param_matrix, sim_atom, &results_double, &results_complete);
    }
    else
    {
        if (verbose > 1){Rcpp::Rcout << "Starting parameters provided\n";}

        // The data in "param_matrix" is already accepted
        // To-do: finish this clause
    }
    // Calculate current parameter SD's
    Eigen::VectorXd tau = (param_matrix.rowwise() - 
                      (param_matrix.colwise()).mean()
                      ).colwise().norm()/std::sqrt((double) 
                        (param_matrix.rows())-1.0);



    // Step 0b: set weights to 1/N
    Eigen::VectorXd w0 = Eigen::VectorXd::Zero(Npart).array() + 1.0/((double) Npart);
    Eigen::VectorXd w1 = Eigen::VectorXd::Zero(Npart).array() + 1.0/((double) Npart);
    Eigen::VectorXd cum_weights = Eigen::VectorXd::Zero(Npart).array() + 1.0/((double) Npart);

    for (iteration = 0; iteration < num_iterations; iteration++)
    {   
        Rcpp::checkUserInterrupt();       
        if (verbose > 0){Rcpp::Rcout << "Iteration " << iteration << ". e0: " << e0 << "\n";}

        // Calculating tau

        tau = (param_matrix.rowwise() - 
              (param_matrix.colwise()).mean()
                ).colwise().norm()/std::sqrt((double) 
                        (param_matrix.rows())-1.0);

        e1 = solve_for_epsilon(results_double.minCoeff() + 1.0,
                                     results_double.maxCoeff(),
                                     //(results_double.rowwise().minCoeff()).maxCoeff(), // Add 1?
                                     e0,
                                     samplingControlInstance -> shrinkage,
                                     results_double,
                                     w0);
        w1 = calculate_weights_DM(e1,
                               e0, 
                               results_double,
                               w0);
        if (verbose > 2)
        {
            Rcpp::Rcout << "   e1 = " << e1 << "\n";
            Rcpp::Rcout << "   w0, 1-10: ";
            for (i = 0; i < std::min(10, (int) w1.size()); i++)
            {
                Rcpp::Rcout << w0(i) << ", ";
            }
            Rcpp::Rcout << "\n";

            Rcpp::Rcout << "   w1 1-10:";
            for (i = 0; i < std::min(10, (int) w1.size()); i++)
            {
                Rcpp::Rcout << w1(i) << ", ";
            }
            Rcpp::Rcout << "\n";
        }
        
        if (ESS(w1) < Npart)
        {
           // Resample Npart particles 
           // Compute cumulative weights
           cum_weights(0) = w1(0);
           for (i = 1; i < w1.size(); i++)
           {
               cum_weights(i) = w1(i) + cum_weights(i-1);
           }
           // Fill in param_matrix with resamples
           for (i = 0; i < Nsim; i++)
           {
               drw = U(*generator);
               for (j = 0; j < Npart; j++)
               {
                    if (drw <= cum_weights(j))
                    {
                        proposed_param_matrix.row(i) = param_matrix.row(j); 
                        proposed_results_double.row(i) = results_double.row(j);
                        break;
                    }
               }
           }
           // Reset weights
           for (i = 0; i < Npart; i++)
           {
               w1(i) = 1.0/((double) Npart);
           } 
        }
        else
        {
            Rcpp::Rcout << "Not Resampling, ESS sufficient.\n";
            // Do nothing
        }

        for (i = 0; i < proposed_results_double.rows(); i++)
        {
            if (proposed_results_double.row(i).minCoeff() > e1)
            {
                Rcpp::Rcout << "Problem: " << i << " e1=" << e1 << ", eps=" <<
                    proposed_results_double.row(i).minCoeff() << "\n";

            }
        }
        // zzz this step won't work if batch_sizes different
        param_matrix = proposed_param_matrix;
        results_double = proposed_results_double;

        // Step 3: MCMC update
        proposal_cache = proposed_param_matrix;
        preproposal_params = proposed_param_matrix;
        preproposal_results = proposed_results_double;

        /*
        run_simulations(proposed_param_matrix, 
                        sim_atom, 
                        &proposed_results_double, 
                        &results_complete);
        */



        // Until all < eps
        int currentIdx = 0;
        int nBatches = 0;
        while (currentIdx < Npart && 
               nBatches < maxBatches)
        {
           preproposal_params = proposal_cache;
           proposeParams(&preproposal_params, 
                         &tau,
                         generator);     

           run_simulations(preproposal_params, sim_atom, &preproposal_results, 
                   &results_complete);
           auto mins = preproposal_results.rowwise().minCoeff();

           for (i = 0; i < Nsim && currentIdx < Npart; i++)
           {
               if (mins(i) < e1)
               {
                   proposed_param_matrix.row(currentIdx) = 
                       preproposal_params.row(i);
                   proposed_results_double.row(currentIdx) = 
                       preproposal_results.row(i);
                   currentIdx++;
               }
           }
           if (currentIdx < Npart && verbose > 1)
           {
                Rcpp::Rcout << "  batch " << nBatches << ", " << currentIdx << 
                    "/" << Npart << " accepted\n";
           }
           nBatches ++;
        }
        if (currentIdx + 1 < results_double.rows())
        {
            Rcpp::Rcout << "  " << currentIdx + 1 << "/" 
                << Npart << " acceptances in " << nBatches << " batches\n";
            // Fill in rest of matrix
            for (i = currentIdx; i < Npart; i++)
            {
                proposed_results_double.row(i) = preproposal_results.row(i);
                proposed_param_matrix.row(i) = preproposal_params.row(i);
            }
        }

        int numAccept = 0;
        int numNan = 0;
        double acc_ratio, num, denom, pn, pd;
        // Nsim == Npart for following code
        for (i = 0; i < Nsim; i++)
        {
            pn = evalPrior(proposed_param_matrix.row(i));
            pd = evalPrior(param_matrix.row(i));
            num = 0.0;
            denom = 0.0;

            for (j = 0; j < results_double.cols(); j++)
            {
                num += (proposed_results_double(i,j) < e1);
                denom += (results_double(i,j) < e1);
            }
            num = num;
            denom = denom;

            num *= pn;
            denom *= pd;

            acc_ratio = num/denom;
            drw = U(*generator);
            if (std::isnan(acc_ratio))
            {
                numNan ++;
            }

            if (!std::isnan(acc_ratio) && drw <= acc_ratio)
            {
                numAccept++;
                param_matrix.row(i) = proposed_param_matrix.row(i);
                results_double.row(i) = proposed_results_double.row(i);
            }
            else
            {
                //param_matrix.row(i) = prev_param_matrix.row(i);
            }
        } 

        if (numAccept == 0)
        {
            Rcpp::Rcout << "WARNING: THE SAMPLER COLLAPSED.\n"; 
        }
        if (verbose > 2)
        {
            Rcpp::Rcout << "    MCMC Step Complete. " << numAccept << " accepted\n";
        }
        e0 = e1;
        w0 = w1;
    }

    // Todo: keep an eye on this object handling. It may have unreasonable
    // overhead, and is kind of complex.  
    Rcpp::List outList;
    if (sim_type_atom == sim_result_atom)
    {
        // keep_samples indicates a debug mode, so don't worry if we can't make
        // a regular data frame from the list.
        for (i = 0; i < (int) results_complete.size(); i++)
        {
            Rcpp::List subList;
            subList["S"] = Rcpp::wrap(results_complete[i].S);
            subList["E"] = Rcpp::wrap(results_complete[i].E);
            subList["I"] = Rcpp::wrap(results_complete[i].I);
            subList["R"] = Rcpp::wrap(results_complete[i].R);

            subList["S_star"] = Rcpp::wrap(results_complete[i].S_star);
            subList["E_star"] = Rcpp::wrap(results_complete[i].E_star);
            subList["I_star"] = Rcpp::wrap(results_complete[i].I_star);
            subList["R_star"] = Rcpp::wrap(results_complete[i].R_star);
            subList["p_se"] = Rcpp::wrap(results_complete[i].p_se);
            // We p_ei and p_ir not generally defined in non-exponential case.  
            if (transitionMode == "exponential")
            {
                subList["p_ei"] = Rcpp::wrap(results_complete[i].p_ei);
                subList["p_ir"] = Rcpp::wrap(results_complete[i].p_ir);
            }

            if (hasSpatial)
            {
                subList["rho"] = Rcpp::wrap(results_complete[i].rho);
            }
            subList["beta"] = Rcpp::wrap(results_complete[i].beta);
            subList["X"] = Rcpp::wrap(results_complete[i].X);
            if (hasReinfection)
            {
                // TODO: output reinfection info
            }
            subList["result"] = Rcpp::wrap(results_complete[i].result);
            outList[std::to_string(i)] = subList;
        }
    }
    else if (sim_type_atom == sim_atom)
    {
        outList["result"] = Rcpp::wrap(results_double);
    }
    outList["params"] = Rcpp::wrap(param_matrix);
    outList["completedEpochs"] = iteration;
    outList["currentEps"] = e1;
    return(outList);
}
