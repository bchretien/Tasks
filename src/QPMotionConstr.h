// This file is part of Tasks.
//
// Tasks is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Tasks is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Tasks.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

// includes
// std
#include <map>

// Eigen
#include <Eigen/Core>

// RBDyn
#include <RBDyn/FD.h>
#include <RBDyn/Jacobian.h>

// Tasks
#include "QPSolver.h"


namespace tasks
{
class TorqueBound;
class PolyTorqueBound;

namespace qp
{

class PositiveLambda : public ConstraintFunction<Bound>
{
public:
	PositiveLambda();

	// Constraint
	virtual void updateNrVars(const std::vector<rbd::MultiBody>& mbs,
		const SolverData& data);
	virtual void update(const std::vector<rbd::MultiBody>& mbs,
		const std::vector<rbd::MultiBodyConfig>& mbc,
		const SolverData& data);

	// Description
	virtual std::string nameBound() const;
	virtual std::string descBound(const std::vector<rbd::MultiBody>& mbs, int line);

	// Bound Constraint
	virtual int beginVar() const;

	virtual const Eigen::VectorXd& Lower() const;
	virtual const Eigen::VectorXd& Upper() const;

private:
	struct ContactData
	{
		ContactId cId;
		int lambdaBegin, nrLambda; // lambda index in x
	};

private:
	int lambdaBegin_;
	Eigen::VectorXd XL_, XU_;

	std::vector<ContactData> cont_; // only usefull for descBound
};


class MotionConstrCommon : public ConstraintFunction<GenInequality>
{
public:
	MotionConstrCommon(const std::vector<rbd::MultiBody>& mbs, int robotIndex);

	void computeTorque(const Eigen::VectorXd& alphaD,
		const Eigen::VectorXd& lambda);
	const Eigen::VectorXd& torque() const;
	void torque(const std::vector<rbd::MultiBody>& mbs,
		std::vector<rbd::MultiBodyConfig>& mbcs) const;

	// Constraint
	virtual void updateNrVars(const std::vector<rbd::MultiBody>& mbs,
		const SolverData& data);

	void computeMatrix(const std::vector<rbd::MultiBody>& mb,
		const std::vector<rbd::MultiBodyConfig>& mbcs);

	// Description
	virtual std::string nameGenInEq() const;
	virtual std::string descGenInEq(const std::vector<rbd::MultiBody>& mbs, int line);

	// Inequality Constraint
	virtual int maxGenInEq() const;

	virtual const Eigen::MatrixXd& AGenInEq() const;
	virtual const Eigen::VectorXd& LowerGenInEq() const;
	virtual const Eigen::VectorXd& UpperGenInEq() const;

protected:
	struct ContactData
	{
		ContactData() {}
		ContactData(const rbd::MultiBody& mb,
			int bodyId, int lambdaBegin,
			std::vector<Eigen::Vector3d> points,
			const std::vector<FrictionCone>& cones);


		int bodyIndex, lambdaBegin;
		rbd::Jacobian jac;
		std::vector<Eigen::Vector3d> points;
		// BEWARE generator are minus to avoid one multiplication by -1 in the
		// update method
		std::vector<Eigen::Matrix<double, 3, Eigen::Dynamic> > minusGenerators;
	};

protected:
	int robotIndex_, alphaDBegin_, nrDof_, lambdaBegin_;
	rbd::ForwardDynamics fd_;
	Eigen::MatrixXd fullJacLambda_, jacTrans_, jacLambda_;
	std::vector<ContactData> cont_;

	Eigen::VectorXd curTorque_;

	Eigen::MatrixXd A_;
	Eigen::VectorXd AL_, AU_;
};


class MotionConstr : public MotionConstrCommon
{
public:
	MotionConstr(const std::vector<rbd::MultiBody>& mbs, int robotIndex,
		const TorqueBound& tb);

	// Constraint
	virtual void update(const std::vector<rbd::MultiBody>& mbs,
		const std::vector<rbd::MultiBodyConfig>& mbcs,
		const SolverData& data);

protected:
	Eigen::VectorXd torqueL_, torqueU_;
};


struct SpringJoint
{
	SpringJoint(){}
	SpringJoint(int jId, double K, double C, double O):
		jointId(jId),K(K),C(C),O(O)
	{}

	int jointId;
	double K, C, O;
};


class MotionSpringConstr : public MotionConstr
{
public:
	MotionSpringConstr(const std::vector<rbd::MultiBody>& mbs,
		int robotIndex, const TorqueBound& tb,
		const std::vector<SpringJoint>& springs);

	// Constraint
	virtual void update(const std::vector<rbd::MultiBody>& mbs,
		const std::vector<rbd::MultiBodyConfig>& mbc,
		const SolverData& data);

protected:
	struct SpringJointData
	{
		int index;
		int posInDof;
		double K;
		double C;
		double O;
	};

protected:
	std::vector<SpringJointData> springs_;
};


/**
 * @brief Use polynome in function of q to compute torque limits.
 * BEWARE: Only work with 1 dof/param joint
 */
class MotionPolyConstr : public MotionConstrCommon
{
public:
	MotionPolyConstr(const std::vector<rbd::MultiBody>& mbs,
		int robotIndex, const PolyTorqueBound& ptb);

	// Constraint
	virtual void update(const std::vector<rbd::MultiBody>& mbs,
		const std::vector<rbd::MultiBodyConfig>& mbcs,
		const SolverData& data);

protected:
	std::vector<Eigen::VectorXd> torqueL_, torqueU_;
	std::vector<int> jointIndex_;
};


} // namespace qp

} // namespace tasks
