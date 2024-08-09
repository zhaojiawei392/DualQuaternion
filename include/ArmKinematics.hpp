#pragma once
#include "Pose.hpp"
#include <memory>

namespace dq1
{

namespace kinematics
{

const Vecxd _closest_invariant_rotation_error(const Rot& r, const Rot& rd)
{
    Rot er_plus = Rot(r.conj() * rd - 1);
    Rot er_minus = Rot(r.conj() * rd + 1);

    double er_plus_norm  = er_plus.norm();
    double er_minus_norm = er_minus.norm();

    double invariant;
    if(er_plus_norm<er_minus_norm)
    {
        // invariant = 1;
        return er_plus.vec4();
    }
    else
    {
        return er_minus.vec4();
        // invariant = -1;
    }
}

class Joint{
protected:
    double pos_; // joint position
    Vec4d limits_; // {max position, min position, max speed, min speed}
    Pose fkm_; // forward kinematics, pose transformation from reference to end    
    Vec8d jcb_; // jacobian of reference end pose with respect to joint position
    void _check_limits(){
        if (limits_[0] < limits_[1] || limits_[2] < limits_[3])
            throw std::runtime_error("Joint complains about unreasonable joint limits, check if max position limits are all bigger than the min limits.\n");
    }
    void _make_position_within_limits(){
        if (pos_ > limits_[0]) 
            pos_ = limits_[0];
        if (pos_ < limits_[1])
            pos_ = limits_[1];
    }

public:

    // Mutable 

    Joint()=delete;
    Joint(const Vec4d& motion_limits, double position=0): limits_(motion_limits), pos_(position), fkm_(1) {
        _check_limits();
    }
    virtual ~Joint();

    virtual void update(double position)noexcept=0;
    virtual void update_signal(double signal)noexcept=0;

    // Const
    virtual Pose calculate_fkm(double position)const =0 ;
    virtual Vec8d calculate_jacobian(double position)const =0 ;

    double position() const noexcept{ return pos_; }
    Vec4d motion_limits()  const noexcept{ return limits_; }
    Pose fkm() const  noexcept{ return fkm_; }
    Vec8d jacobian() const noexcept{ return jcb_; } 
};

class RevoluteJoint: public Joint{
protected:
    Vec4d DH_params_; // {Theta, d, a, alpha}
public:

    // Mutable 

    RevoluteJoint()=delete;
    RevoluteJoint(const Vec4d& DH_parameters, const Vec4d& motion_limits, double position=0): Joint(motion_limits, position), DH_params_(DH_parameters){
        update(position);
    }
    virtual ~RevoluteJoint()=default;


    virtual void update(double position)noexcept{
        pos_ = position;
        _make_position_within_limits();
        fkm_ = calculate_fkm(pos_);
        jcb_ = calculate_jacobian(pos_);
    }
    virtual void update_signal(double signal)noexcept{
        pos_ += signal;
        _make_position_within_limits();
        fkm_ = calculate_fkm(pos_);
        jcb_ = calculate_jacobian(pos_);
    }

    // Const
    virtual Pose calculate_fkm(double position) const override{

        double theta_real = cos( 0.5* (DH_params_[0] + pos_) );
        double theta_im = sin( 0.5* (DH_params_[0] + pos_) );
        double alpha_real = cos( 0.5 * DH_params_[3] );
        double alpha_im = sin( 0.5 * DH_params_[3] );
        double d = DH_params_[1];
        double a = DH_params_[2];
        return Pose{
            theta_real * alpha_real,
            theta_real * alpha_im,
            theta_im * alpha_im,
            theta_im * alpha_real,
            0.5 * ( -theta_im * d * alpha_real - theta_real * a * alpha_im ),
            0.5 * ( -theta_im * d * alpha_im + theta_real * a * alpha_real ),
            0.5 * ( theta_im * a * alpha_real + theta_real * d * alpha_im ),
            0.5 * (theta_real * d * alpha_real - theta_im * a * alpha_im)
        }; // test past
    }
    virtual Vec8d calculate_jacobian(double position) const override{

        double theta_dot_real = -0.5 * sin( 0.5* (DH_params_[0] + pos_) );
        double theta_dot_im = 0.5 * cos( 0.5* (DH_params_[0] + pos_) );
        double alpha_real = cos( 0.5 * DH_params_[3] );
        double alpha_im = sin( 0.5 * DH_params_[3] );
        double d = DH_params_[1];
        double a = DH_params_[2];
        return Vec8d{
            theta_dot_real * alpha_real,
            theta_dot_real * alpha_im,
            theta_dot_im * alpha_im,
            theta_dot_im * alpha_real,
            0.5 * ( -theta_dot_im * d * alpha_real - theta_dot_real * a * alpha_im ),
            0.5 * ( -theta_dot_im * d * alpha_im + theta_dot_real * a * alpha_real ),
            0.5 * ( theta_dot_im * a * alpha_real + theta_dot_real * d * alpha_im ),
            0.5 * (theta_dot_real * d * alpha_real - theta_dot_im * a * alpha_im)
        }; // test past
    }

};

class PrismaticJoint: public Joint{
protected:
    Vec4d DH_params_; // {Theta, d, a, alpha}
public:

    // Mutable 

    PrismaticJoint()=delete;
    PrismaticJoint(const Vec4d& DH_parameters, const Vec4d& motion_limits, double position);
    virtual ~PrismaticJoint()=default;

    virtual void update(double position)noexcept{}
    // Const
    virtual Pose calculate_fkm(double position) const override {

    }
    virtual Vec8d calculate_jacobian(double position) const override{

    }
};


class SerialManipulator{
protected:
    std::vector<std::unique_ptr<Joint>> joints_;
    Pose base_;
    Pose effector_;
    Pose abs_end_;
    std::vector<Pose> joint_poses_; // joint poses + end pose
    Pose_jcb pose_jacobian_;
    Rot_jcb r_jacobian_;
    Tslt_jcb t_jacobian_;
public:
    SerialManipulator()=delete;
    SerialManipulator(const Matd<5, -1>& DH_params, const Matd<4, -1>& joint_limits, const Vecxd& joint_positions){
        if (DH_params.cols() == 0 ){
            throw std::runtime_error("SerialManipulator(const Matxd& DH_params, const Matxd& joint_limits, const Vecxd& joint_positions) arg0 cols invalid size 0, should be DoF bigger than 0.\n");
        }
        int DOF = DH_params.cols();
        check_size("SerialManipulator(const Matd<5, -1>& DH_params, const Matd<4, -1>& joint_limits, const Vecxd& joint_positions) arg1 cols", "DoF",joint_limits.cols(), DOF);
        check_size("SerialManipulator(const Matd<5, -1>& DH_params, const Matd<4, -1>& joint_limits, const Vecxd& joint_positions) arg2", "DoF",joint_positions.size(), DOF);
        
        joint_poses_.resize(DOF);
        pose_jacobian_.resize(8, DOF);
        r_jacobian_.resize(4, DOF);
        t_jacobian_.resize(4, DOF);


        // instantiate remaining joints
        for (int i=0; i<DOF; ++i){
            if (DH_params[4, i] == 0){
                joints_.push_back(std::make_unique<RevoluteJoint>(DH_params.block<4,1>(0,i), joint_limits.block<4,1>(0,i), joint_positions[i]));
            } else if (DH_params[4, i] == 1){
                joints_.push_back(std::make_unique<PrismaticJoint>(DH_params.block<4,1>(0,i), joint_limits.block<4,1>(0,i), joint_positions[i]));
            } else {
                throw std::runtime_error("SerialManipulator(const Matxd& DH_params, const Matxd& joint_limits, const Vecxd& joint_positions) arg0 5-th row DH_params[4, " + std::to_string(i) + "] = " + std::to_string(DH_params[4,i]) + ", this row should consist only of 0 or 1.(0: Revolute joint, 1: Prismatic joint)\n");
            }
        }

        std::cout << "A Kinematics::SerialManipulator constructed!\n" ;
    }
    virtual ~SerialManipulator()=default;

    void set_base(const Pose& base) noexcept{
        base_ = base;
    } 
    void set_effector(const Pose& effector) noexcept{
        effector_ = effector;
    } 

    void update(const Pose& desired_pose){


    update_joint_signals(u);
    }

    void update_joint_positions(const Vecxd& joint_positions){
        check_size("update_joint_positions(const Vecxd& joint_positions)", "DoF", joint_positions.size(), joints_.size());
        
        joints_[0]->update(joint_positions[0]);
        joint_poses_[0] = joints_[0]->fkm();

        for (int i=1; i<joints_.size(); ++i){
            joints_[i]->update(joint_positions[i]);
            joint_poses_[i] = joint_poses_[i-1] * joints_[i]->fkm();
        }
        abs_end_ = base_ * joint_poses_.back() * effector_;
        _update_jacobians();
    }

    void update_joint_signals(const Vecxd& joint_signals){
        check_size("update_joint_positions(const Vecxd& joint_signals)", "DoF", joint_signals.size(), joints_.size());
        
        joints_[0]->update_signal(joint_signals[0]);
        joint_poses_[0] = joints_[0]->fkm();

        for (int i=1; i<joints_.size(); ++i){
            joints_[i]->update_signal(joint_signals[i]);
            joint_poses_[i] = joint_poses_[i-1] * joints_[i]->fkm();
        }
        abs_end_ = base_ * joint_poses_.back() * effector_;
        _update_jacobians();
    }

    Vecxd joint_positions() const noexcept{
        Vecxd res;
        for (const auto& joint : joints_){
            res << joint->position();
        }
        return std::move(res);
    }
    // Vecxd joint_signals() const noexcept;
private:
    Matxd _calculate_quadratic_matrix(const Pose& desired_pose){
        const Matxd& r_rd_jacobian = desired_pose.rotation().conj().haminus() * r_jacobian_;
        const Matxd& Ht = (t_jacobian_.transpose() * t_jacobian_) * 2;
        const Matxd& Hr = (r_rd_jacobian.transpose() * r_rd_jacobian) * 2;
        const Vecxd& damping_vec = Vecxd::Ones(joints_.size()) * 0.0001;
        const Matxd& Hj = damping_vec.asDiagonal();
        return 0.9999 * Ht + (1-0.9999) * Hr + Hj;
    }
    Vecxd _calculate_quadratic_vector(const Pose& desired_pose){
        const Matxd& r_rd_jacobian = desired_pose.rotation().conj().haminus() * r_jacobian_;
        const Vecxd& vec_et = (abs_end_.translation() - desired_pose.translation()).vec4();
        const Vecxd& vec_er = _closest_invariant_rotation_error(abs_end_.rotation(), desired_pose.rotation());
        const Vecxd& ct = 2 * vec_et.transpose() * 50 * t_jacobian_;
        const Vecxd& cr = 2 * vec_er.transpose() * 50 * r_rd_jacobian;
        return 0.9999 * ct + (1-0.9999) * cr;
    }
    Matxd _update_jacobians() {
        for (int i=0; i<joints_.size(); ++i){
            const Vec8d& pose_jacobian_i = (joint_poses_[i].conj() * joint_poses_.back()).haminus() * joints_[i]->jacobian();
            pose_jacobian_.col(i) = pose_jacobian_i;
        }
        pose_jacobian_ = effector_.haminus() * base_.hamiplus() * pose_jacobian_;

        r_jacobian_ = pose_jacobian_.block(0,0,4,joints_.size());

        t_jacobian_ = abs_end_.rotation().conj().haminus() * (pose_jacobian_.block(4,0,4,joints_.size()) * 2 - abs_end_.translation().hamiplus() * r_jacobian_);
    }


};

}
    
}