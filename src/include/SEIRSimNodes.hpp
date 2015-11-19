#ifndef ACTOR_SEIRSIM_HEADER
#define ACTOR_SEIRSIM_HEADER
#include <map>
#include <vector>
#include <random>
#include <sstream>
#include <iostream>
#include <Eigen/Core>
#include <dataModel.hpp>
#include "caf/all.hpp"

using namespace std;
using namespace caf;

using sim_atom = atom_constant<atom("sim")>;
using sim_result_atom = atom_constant<atom("sim_rslt")>;
using sample_atom = atom_constant<atom("sample")>;
using wakeup_atom = atom_constant<atom("wakeup")>;
using exit_atom = atom_constant<atom("exit")>;

struct simulationResultSet;
class transitionDistribution;

class SEIR_sim_node : public event_based_actor {
    public:
        SEIR_sim_node(int random_seed,
                      Eigen::VectorXi S0,
                      Eigen::VectorXi E0,
                      Eigen::VectorXi I0,
                      Eigen::VectorXi R0,
                      Eigen::VectorXd offset,
                      Eigen::MatrixXi Y,
                      MatrixXb na_mask,
                      std::vector<Eigen::MatrixXd> DM_vec,
                      Eigen::MatrixXd X, 
                      Eigen::MatrixXd X_rs,
                      std::string transitionMode,
                      Eigen::MatrixXd ei_prior,
                      Eigen::MatrixXd ir_prior,
                      double avgI,
                      Eigen::VectorXd sp_prior,
                      Eigen::VectorXd se_prec,
                      Eigen::VectorXd rs_prec,
                      Eigen::VectorXd se_mean,
                      Eigen::VectorXd rs_mean,
                      double phi,
                      int data_compartment,
                      bool cumulative,
                      actor parent);
        ~SEIR_sim_node();
    protected:
        simulationResultSet simulate(Eigen::VectorXd param_vals, bool keepCompartments);
        behavior make_behavior() override;
    private: 
        void calculateReproductiveNumbers(simulationResultSet* input);
        int sim_width;
        unsigned int random_seed;
        Eigen::VectorXi S0;
        Eigen::VectorXi E0;
        Eigen::VectorXi I0;
        Eigen::VectorXi R0;
        Eigen::VectorXd offset;
        Eigen::MatrixXi Y;
        Eigen::MatrixXi E_paths;
        Eigen::MatrixXi I_paths;
        MatrixXb na_mask;
        std::vector<Eigen::MatrixXd> DM_vec;
        Eigen::MatrixXd X;
        Eigen::MatrixXd X_rs;
        std::string transitionMode;
        Eigen::MatrixXd E_to_I_prior;
        Eigen::MatrixXd I_to_R_prior;
        Eigen::VectorXd spatial_prior;
        Eigen::VectorXd exposure_precision;
        Eigen::VectorXd reinfection_precision;
        Eigen::VectorXd exposure_mean;
        Eigen::VectorXd reinfection_mean;
        /** General E to I transition Distribution*/
        std::unique_ptr<transitionDistribution> EI_transition_dist;
        /** General I to R transition Distribution*/
        std::unique_ptr<transitionDistribution> IR_transition_dist;
        double phi;
        int seed;
        double value;
        double inf_mean;
        bool has_spatial;
        bool has_reinfection;
        int total_size;
        int data_compartment;
        bool cumulative;
        actor parent;
        scoped_actor* self;
        mt19937* generator;
        std::normal_distribution<double> overdispersion_distribution;
        behavior    alive;
};




#endif