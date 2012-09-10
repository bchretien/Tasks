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

// associated header
#include "QPConstr.h"

// includes
// RBDyn
#include <MultiBody.h>
#include <MultiBodyConfig.h>

// SCD
#include <SCD/CD/CD_Pair.h>
#include <SCD/S_Object/S_Object.h>

namespace tasks
{

namespace qp
{

MotionConstr::MotionConstr(const rbd::MultiBody& mb):
	fd_(mb),
	cont_(),
	fullJac_(6, mb.nrDof()),
	AEq_(),
	BEq_(),
	XL_(),
	XU_(),
	nrDof_(0),
	nrFor_(0),
	nrTor_(0)
{
}


void MotionConstr::updateNrVars(const rbd::MultiBody& mb,
	int alphaD, int lambda, int torque, const std::vector<Contact>& cont)
{
	cont_.resize(cont.size());

	nrDof_ = alphaD;
	nrFor_ = lambda;
	nrTor_ = torque;

	for(std::size_t i = 0; i < cont_.size(); ++i)
	{
		cont_[i].jac = rbd::Jacobian(mb, cont[i].bodyId);
		cont_[i].transJac.resize(6, cont_[i].jac.dof());
		cont_[i].points = cont[i].points;
		cont_[i].normals = cont[i].normals;
	}

	AEq_.resize(nrDof_, nrDof_ + nrFor_ + nrTor_);
	BEq_.resize(nrDof_);

	AEq_.setZero();
	BEq_.setZero();

	XL_.resize(nrFor_);
	XU_.resize(nrFor_);

	XL_.fill(0.);
	XU_.fill(std::numeric_limits<double>::infinity());
}


void MotionConstr::update(const rbd::MultiBody& mb, const rbd::MultiBodyConfig& mbc)
{
	using namespace Eigen;

	fd_.computeH(mb, mbc);
	fd_.computeC(mb, mbc);

	// H*alphaD - tau - tau_c = -C

	// AEq
	//         nrDof      nrFor            nrTor
	// nrDof [   H      -Sum J_i^t*ni     [0 ... -1]

	AEq_.block(0, 0, nrDof_, nrDof_) = fd_.H();

	int contPos = nrDof_;
	for(std::size_t i = 0; i < cont_.size(); ++i)
	{
		const MatrixXd& jac = cont_[i].jac.jacobian(mb, mbc);

		for(std::size_t j = 0; j < cont_[i].points.size(); ++j)
		{
			cont_[i].jac.translateJacobian(jac, mbc, cont_[i].points[j], cont_[i].transJac);
			cont_[i].jac.fullJacobian(mb, cont_[i].transJac, fullJac_);

			AEq_.block(0, contPos, nrDof_, 1) =
				-fullJac_.block(3, 0, 3, fullJac_.cols()).transpose()*cont_[i].normals[j];

			contPos += 1;
		}
	}

	AEq_.block(mb.joint(0).dof(), contPos, nrTor_, nrTor_) =
		-MatrixXd::Identity(nrTor_, nrTor_);


	// BEq = -C
	BEq_ = -fd_.C();
}


int MotionConstr::nrEqLine()
{
	return nrDof_;
}


const Eigen::MatrixXd& MotionConstr::AEq() const
{
	return AEq_;
}


const Eigen::VectorXd& MotionConstr::BEq() const
{
	return BEq_;
}


int MotionConstr::beginVar()
{
	return nrDof_;
}


const Eigen::VectorXd& MotionConstr::Lower() const
{
	return XL_;
}


const Eigen::VectorXd& MotionConstr::Upper() const
{
	return XU_;
}



ContactAccConstr::ContactAccConstr(const rbd::MultiBody& mb):
	cont_(),
	fullJac_(6, mb.nrDof()),
	alphaVec_(mb.nrDof()),
	AEq_(),
	BEq_(),
	nrDof_(0),
	nrFor_(0),
	nrTor_(0)
{}


void ContactAccConstr::updateNrVars(const rbd::MultiBody& mb,
	int alphaD, int lambda, int torque, const std::vector<Contact>& cont)
{
	cont_.resize(cont.size());

	nrDof_ = alphaD;
	nrFor_ = lambda;
	nrTor_ = torque;

	for(std::size_t i = 0; i < cont_.size(); ++i)
	{
		cont_[i].jac = rbd::Jacobian(mb, cont[i].bodyId);
	}

	AEq_.resize(cont_.size()*6 , nrDof_ + nrFor_ + nrTor_);
	BEq_.resize(cont_.size()*6);

	AEq_.setZero();
	BEq_.setZero();
}


void ContactAccConstr::update(const rbd::MultiBody& mb, const rbd::MultiBodyConfig& mbc)
{
	using namespace Eigen;

	rbd::paramToVector(mbc.alpha, alphaVec_);

	// J_i*alphaD + JD_i*alpha = 0

	for(std::size_t i = 0; i < cont_.size(); ++i)
	{
		// AEq = J_i
		const MatrixXd& jac = cont_[i].jac.jacobian(mb, mbc);
		cont_[i].jac.fullJacobian(mb, jac, fullJac_);
		AEq_.block(i*6, 0, 6, mb.nrDof()) = fullJac_;

		// BEq = -JD_i*alpha
		const MatrixXd& jacDot = cont_[i].jac.jacobianDot(mb, mbc);
		cont_[i].jac.fullJacobian(mb, jacDot, fullJac_);
		BEq_.segment(i*6, 6) = -fullJac_*alphaVec_;
	}
}


int ContactAccConstr::nrEqLine()
{
	return AEq_.rows();
}


const Eigen::MatrixXd& ContactAccConstr::AEq() const
{
	return AEq_;
}


const Eigen::VectorXd& ContactAccConstr::BEq() const
{
	return BEq_;
}


/**
	*													SelfCollisionConstr
	*/


SCD::Matrix4x4 fromSCD(const sva::PTransform& t)
{
	SCD::Matrix4x4 m;
	const Eigen::Matrix3d& rot = t.rotation();
	const Eigen::Vector3d& tran = t.translation();

	for(int i = 0; i < 3; ++i)
	{
		for(int j = 0; j < 3; ++j)
		{
			m(i,j) = rot(i,j);
		}
	}

	m(0,3) = tran(0);
	m(1,3) = tran(1);
	m(2,3) = tran(2);

	m(3,0) = m(3,1) = m(3,2) = 0.;
	m(3,3) = 1.;
	return m;
}


SelfCollisionConstr::CollData::CollData(const rbd::MultiBody& mb,
	int body1Id, SCD::S_Object* body1,
	int body2Id, SCD::S_Object* body2,
	double di, double ds, double damping):
		pair(new SCD::CD_Pair(body1, body2)),
		normVecDist(Eigen::Vector3d::Zero()),
		jacB1(rbd::Jacobian(mb, body1Id)),
		jacB2(rbd::Jacobian(mb, body2Id)),
		di(di),
		ds(ds),
		damping(damping),
		body1Id(body1Id),
		body2Id(body2Id),
		body1(mb.bodyIndexById(body1Id)),
		body2(mb.bodyIndexById(body2Id))
{
}


SelfCollisionConstr::CollData::~CollData()
{
  delete pair;
}


SelfCollisionConstr::SelfCollisionConstr(const rbd::MultiBody& mb, double step):
  dataVec_(),
  step_(step),
  nrVars_(0),
  AInEq_(),
  BInEq_(),
  fullJac_(6, mb.nrDof()),
  fullJacDot_(6, mb.nrDof()),
  alphaVec_(mb.nrDof()),
  calcVec_(mb.nrDof())
{
}


void SelfCollisionConstr::addCollision(const rbd::MultiBody& mb,
																				int body1Id, SCD::S_Object* body1,
																				int body2Id, SCD::S_Object* body2,
																				double di, double ds, double damping)
{
	dataVec_.emplace_back(mb, body1Id, body1, body2Id, body2, di, ds, damping);
}


void SelfCollisionConstr::rmCollision(int body1Id, int body2Id)
{
	auto it = std::find_if(dataVec_.begin(), dataVec_.end(),
		[body1Id, body2Id](const CollData& data)
		{
			return data.body1Id == body1Id && data.body2Id == body2Id;
		});

	if(it != dataVec_.end())
	{
		dataVec_.erase(it);
	}
}


void SelfCollisionConstr::reset()
{
	dataVec_.clear();
}


void SelfCollisionConstr::updateNrVars(const rbd::MultiBody& /* mb */,
	int alphaD, int lambda, int torque, const std::vector<Contact>& /* cont */)
{
	nrVars_ = alphaD + lambda + torque;
}


void SelfCollisionConstr::update(const rbd::MultiBody& mb, const rbd::MultiBodyConfig& mbc)
{
	using namespace Eigen;

	if(static_cast<unsigned int>(AInEq_.rows()) != dataVec_.size()
		 || AInEq_.cols() != nrVars_)
	{
		AInEq_.resize(dataVec_.size(), nrVars_);
		BInEq_.resize(dataVec_.size());
		AInEq_.setZero();
		BInEq_.setZero();
	}

	rbd::paramToVector(mbc.alpha, alphaVec_);

	int i = 0;
	for(CollData& d: dataVec_)
	{
		SCD::Point3 pb1Tmp, pb2Tmp;

		(*d.pair)[0]->setTransformation(fromSCD(mbc.bodyPosW[d.body1]));
		(*d.pair)[1]->setTransformation(fromSCD(mbc.bodyPosW[d.body2]));

		double dist = d.pair->getClosestPoints(pb1Tmp, pb2Tmp);
		dist = dist >= 0 ? std::sqrt(dist) : -std::sqrt(-dist);

		Vector3d pb1(pb1Tmp[0], pb1Tmp[1], pb1Tmp[2]);
		Vector3d pb2(pb2Tmp[0], pb2Tmp[1], pb2Tmp[2]);

		Eigen::Vector3d normVecDist = (pb1 - pb2)/dist;

		pb1 = (mbc.bodyPosW[d.body1].inv()*sva::PTransform(pb1)).translation();
		pb2 = (mbc.bodyPosW[d.body2].inv()*sva::PTransform(pb2)).translation();

		if(dist < d.di)
		{
			double dampers = d.damping*((dist - d.ds)/(d.di - d.ds));

			Vector3d nf = normVecDist;
			Vector3d onf = d.normVecDist;
			Vector3d dnf = (nf - onf)/step_;

			// Compute body1
			d.jacB1.point(pb1);
			const MatrixXd& jac1 = d.jacB1.jacobian(mb, mbc);
			const MatrixXd& jacDot1 = d.jacB1.jacobianDot(mb, mbc);

			d.jacB1.fullJacobian(mb, jac1, fullJac_);
			d.jacB1.fullJacobian(mb, jacDot1, fullJacDot_);

			double jqdn = ((fullJac_.block(3, 0, 3, fullJac_.cols())*alphaVec_).transpose()*nf)(0);
			double jqdnd = ((fullJac_.block(3, 0, 3, fullJac_.cols())*alphaVec_).transpose()*dnf*step_)(0);
			double jdqdn = ((fullJacDot_.block(3, 0, 3, fullJac_.cols())*alphaVec_).transpose()*nf*step_)(0);

			calcVec_ = -fullJac_.block(3, 0, 3, fullJac_.cols()).transpose()*nf*step_;

			// Compute body2
			d.jacB2.point(pb2);
			const MatrixXd& jac2 = d.jacB2.jacobian(mb, mbc);
			const MatrixXd& jacDot2 = d.jacB2.jacobianDot(mb, mbc);

			d.jacB2.fullJacobian(mb, jac2, fullJac_);
			d.jacB2.fullJacobian(mb, jacDot2, fullJacDot_);

			jqdn = -((fullJac_.block(3, 0, 3, fullJac_.cols())*alphaVec_).transpose()*nf)(0);
			jqdnd = -((fullJac_.block(3, 0, 3, fullJac_.cols())*alphaVec_).transpose()*dnf*step_)(0);
			jdqdn = -((fullJacDot_.block(3, 0, 3, fullJac_.cols())*alphaVec_).transpose()*nf*step_)(0);

			calcVec_ += fullJac_.block(3, 0, 3, fullJac_.cols()).transpose()*nf*step_;

			// distdot + distdotdot*dt > -damp*((d - ds)/(di - ds))
			AInEq_.block(i, 0, 1, mb.nrDof()) = calcVec_.transpose();
			BInEq_(i) = dampers + jqdn + jqdnd + jdqdn;
		}
		else
		{
			AInEq_.block(i, 0, 1, mb.nrDof()).setZero();
			BInEq_(i) = 0.;
		}

		d.normVecDist = normVecDist;
		++i;
	}
}


int SelfCollisionConstr::nrInEqLine()
{
	return dataVec_.size();
}


const Eigen::MatrixXd& SelfCollisionConstr::AInEq() const
{
	return AInEq_;
}


const Eigen::VectorXd& SelfCollisionConstr::BInEq() const
{
	return BInEq_;
}


} // namespace qp

} // namespace tasks