// Xerus - A General Purpose Tensor Library
// Copyright (C) 2014-2016 Benjamin Huber and Sebastian Wolf. 
// 
// Xerus is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
// 
// Xerus is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Affero General Public License for more details.
// 
// You should have received a copy of the GNU Affero General Public License
// along with Xerus. If not, see <http://www.gnu.org/licenses/>.
//
// For further information on Xerus visit https://libXerus.org 
// or contact us at contact@libXerus.org.


#include <xerus.h>

#include "../../include/xerus/misc/test.h"

using namespace xerus;

static Tensor::DimensionTuple random_dimensions(const size_t _degree, const size_t _maxDim, std::mt19937_64 _rnd) {
	std::uniform_int_distribution<size_t> dist(1, _maxDim);
	Tensor::DimensionTuple dims;
	for(size_t i = 0; i < _degree; ++i) { dims.emplace_back(dist(_rnd)); }
	return dims;
}

static misc::UnitTest tensor_constructors("Tensor", "Constructors", [](){
	UNIT_TEST_RND;
	std::vector<Tensor> tensors;
	tensors.emplace_back();
	tensors.push_back(tensors.back());
	tensors.emplace_back(Tensor::Representation::Sparse);
	tensors.push_back(tensors.back());
	
	Tensor::DimensionTuple fixedDimensions = random_dimensions(10, 4, rnd);
	const size_t dimensionProduct = misc::product(fixedDimensions); // NOTE it is necessary to calculate this here to prevent internal segfault in gcc 4.8.1
	
	tensors.emplace_back(fixedDimensions, Tensor::Representation::Dense, Tensor::Initialisation::Zero);
	tensors.push_back(tensors.back());
	tensors.emplace_back(fixedDimensions, Tensor::Representation::Sparse, Tensor::Initialisation::Zero);
	tensors.push_back(tensors.back());
	tensors.emplace_back(fixedDimensions, Tensor::Representation::Dense, Tensor::Initialisation::None);
	tensors.push_back(tensors.back());
	tensors.emplace_back(fixedDimensions, Tensor::Representation::Sparse, Tensor::Initialisation::None);
	tensors.push_back(tensors.back());
	
	tensors.emplace_back(random_dimensions(10, 4, rnd), Tensor::Representation::Dense, Tensor::Initialisation::Zero);
	tensors.push_back(tensors.back());
	tensors.emplace_back(random_dimensions(10, 4, rnd), Tensor::Representation::Sparse, Tensor::Initialisation::Zero);
	tensors.push_back(tensors.back());
	tensors.emplace_back(random_dimensions(10, 4, rnd), Tensor::Representation::Dense, Tensor::Initialisation::None);
	tensors.push_back(tensors.back());
	tensors.emplace_back(random_dimensions(10, 4, rnd), Tensor::Representation::Sparse, Tensor::Initialisation::None);
	tensors.push_back(tensors.back());
	
	tensors.emplace_back(Tensor::random(fixedDimensions, rnd, normalDist));
	tensors.push_back(tensors.back());
	tensors.emplace_back(Tensor::random(fixedDimensions, 7, rnd, normalDist));
	tensors.push_back(tensors.back());
	
	tensors.emplace_back(Tensor::random(random_dimensions(10, 4, rnd), rnd, normalDist));
	tensors.push_back(tensors.back());
	tensors.emplace_back(Tensor::random(random_dimensions(10, 4, rnd), 7, rnd, normalDist));
	tensors.push_back(tensors.back());
	
	tensors.emplace_back(fixedDimensions, []()->value_t{ return 0.0; });
	tensors.push_back(tensors.back());
	tensors.emplace_back(Tensor(fixedDimensions, dimensionProduct, [](const size_t _n, const size_t _N)->std::pair<size_t, value_t>{ return std::pair<size_t, value_t>(_n, value_t(_n)); }));
	tensors.push_back(tensors.back());
	
	tensors.emplace_back(fixedDimensions, [](const size_t _i)->value_t{ return value_t(_i); });
	tensors.push_back(tensors.back());
	
	tensors.emplace_back(fixedDimensions, [=](const Tensor::MultiIndex& _i)->value_t{ return value_t(Tensor::multiIndex_to_position(_i, fixedDimensions)); });
	tensors.push_back(tensors.back());
	
	
	for(size_t i = 0; i < tensors.size(); ++i) {
		// Test defaults being degree zero
		if(i < 4) {
			MTEST(tensors[i].degree() == 0, i);
		} else {
			MTEST(tensors[i].degree() == 10, i);
		}
		
		// Test degree calculation
		MTEST(tensors[i].degree() == tensors[i].dimensions.size(), i);
		
		// Test size calcualtion
		MTEST(tensors[i].size == misc::product(tensors[i].dimensions), i);
		
		// Test representation
		if ((i>=2 && (i/2)%2 == 0) || i >= 32) {
			MTEST(tensors[i].is_dense() && !tensors[i].is_sparse(), i);
		} else {
			MTEST(!tensors[i].is_dense() && tensors[i].is_sparse(), i);
		}
		
		// Test zero Initialisation
		if(i < 8 || (i >= 12 && i < 14) || i == 28 || i == 29) {
			MTEST(approx_entrywise_equal(tensors[i], std::vector<value_t>(tensors[i].size, 0.0)), i);
		}
		
		// Test entries
		if(i >= 30 && i < 36) {
			std::vector<value_t> v(tensors[i].size);
			std::iota(v.begin(), v.end(), 0);
			MTEST(approx_entrywise_equal(tensors[i], v), i);
		}
		
		// Test equivalence
		if(!(8 <= i && i < 12) && !(16 <= i && i < 20)) { // Skip uninitialized tensors (inf, and nan may occur)
			if(i%2 == 0) {
				MTEST(approx_equal(tensors[i], tensors[i+1]), i);
				MTEST(approx_entrywise_equal(tensors[i], tensors[i+1]), i);
			} else {
				MTEST(approx_equal(tensors[i], tensors[i-1]), i);
				MTEST(approx_entrywise_equal(tensors[i], tensors[i-1]), i);
			}
		}
	}
	
	fixedDimensions[7] = 0;
	
	FAILTEST(Tensor(fixedDimensions, Tensor::Representation::Dense, Tensor::Initialisation::Zero));
	FAILTEST(Tensor(fixedDimensions, Tensor::Representation::Sparse, Tensor::Initialisation::Zero));
	FAILTEST(auto x = Tensor::random(fixedDimensions, rnd, normalDist));
	FAILTEST(auto x = Tensor::random(fixedDimensions, 7, rnd, normalDist));
	FAILTEST(auto x = Tensor::random(fixedDimensions, rnd, normalDist));
	FAILTEST(auto x = Tensor::random(fixedDimensions, 7, rnd, normalDist));
	FAILTEST(Tensor(fixedDimensions, []()->value_t{ return 0.0; }));
	FAILTEST(Tensor(fixedDimensions, misc::product(fixedDimensions), [](const size_t _n, const size_t _N)->std::pair<size_t, value_t>{ return std::pair<size_t, value_t>(_n, value_t(_n)); }));
	FAILTEST(Tensor(fixedDimensions, [](const size_t _i)->value_t{ return value_t(_i); }));
	FAILTEST(Tensor(fixedDimensions, [=](const Tensor::MultiIndex& _i)->value_t{ return value_t(Tensor::multiIndex_to_position(_i, fixedDimensions)); }));
});



static misc::UnitTest tensor_sparse_dense("Tensor", "Sparse_Dense_Conversions", [](){
	UNIT_TEST_RND;
	Tensor n({3,3,3,3});
	const size_t dim = 100;
	MTEST(frob_norm(n) < 1e-20, "This should be a sparse tensor with no entries, so frob norm exactly = 0!");
	MTEST(n.representation == Tensor::Representation::Sparse, "0-Tensor should be stored as sparse tensor");
	
	std::vector<Tensor> columns;
	for (size_t i=0; i<dim; ++i) {
		columns.emplace_back(std::vector<size_t>({dim,dim}), dim, [&](const size_t _n, const size_t _N)->std::pair<size_t, value_t>{ return std::pair<size_t, value_t>(_n*dim+i, 1.0); });
		MTEST(columns.back().representation == Tensor::Representation::Sparse, "sparse constructor should construct sparse tensor");
	}
	
	Index i1,i2,i3,i4,i5;
	Tensor res({dim,dim}, Tensor::Representation::Sparse);
	
	res({i1,i3}) = columns[0](i1,i2) * columns[0](i3,i2);
	MTEST(frob_norm(res - Tensor::ones({dim,dim})) < 1e-14, "dyadic product should result in ones tensor");
	MTEST(res.representation == Tensor::Representation::Dense, "tensor with every entry == 1 should be stored as dense tensor");
	
	res = Tensor({dim,dim}, Tensor::Representation::Dense);
	res({i1,i3}) = columns[1](i1,i2) * columns[0](i3,i2);
	MTEST(frob_norm(res) < 1e-20, "this should be a sparse tensor with no entries, so frob norm exactly = 0!");
	MTEST(res.representation == Tensor::Representation::Sparse, "this should be a sparse tensor with no entries");
	
	res = Tensor({dim,dim}, Tensor::Representation::Sparse);
	for (size_t i=0; i<dim; ++i) {
		res({i1,i2}) = res({i1,i2}) + columns[i]({i1,i2});
	}
	MTEST(frob_norm(res - Tensor::ones({dim,dim})) < 1e-14, "sum of columns should result in ones tensor");
	MTEST(res.representation == Tensor::Representation::Dense, "tensor with every entry == 1 should be stored as dense tensor");
	
	res = Tensor({dim,dim}, Tensor::Representation::Dense);
	Tensor d = Tensor::random({1}, rnd, normalDist);
	Tensor e = columns[0];
	e.reinterpret_dimensions({dim,dim,1});
	res({i1,i2}) = e(i1,i2,i3) * d(i3);
	MTEST(res.representation == Tensor::Representation::Sparse, "Sparse * full == sparse contractions?");
	
	// assigning sparse to dense tensors and vice versa
	d = Tensor::random({dim,dim}, rnd, normalDist);
	TEST(d.representation == Tensor::Representation::Dense);
	d(i1,i2) = columns[2](i2,i1);
	MTEST(d.representation == Tensor::Representation::Sparse, "sparse tensor assignment");
	d = columns[2];
	MTEST(d.representation == Tensor::Representation::Sparse, "sparse tensor assignment 2");
	
	e = Tensor::random({dim,dim}, rnd, normalDist);
	MTEST(e.representation == Tensor::Representation::Dense, "dense tensor assignment");
	d(i1,i2) = e(i2,i1);
	MTEST(d.representation == Tensor::Representation::Dense, "dense tensor assignment 2");
	d = e;
	MTEST(d.representation == Tensor::Representation::Dense, "dense tensor assignment 3");
	
	// decompositions
	Tensor U({dim,dim}, Tensor::Representation::Sparse);
	Tensor Vt({dim,dim}, Tensor::Representation::Sparse);
	Tensor S({dim,dim}, Tensor::Representation::Dense);
	(U(i1,i2), S(i2,i3), Vt(i3,i4)) = SVD(e(i1,i4));
	MTEST(U.representation == Tensor::Representation::Dense, "singular vectors of SVD 1");
	MTEST(Vt.representation == Tensor::Representation::Dense, "singular vectors of SVD 2");
	MTEST(S.representation == Tensor::Representation::Sparse, "singular values of SVD");
	
	Tensor Q({dim,dim}, Tensor::Representation::Sparse);
	Tensor R({dim,dim}, Tensor::Representation::Sparse);
	(Q(i1,i2), R(i2,i3)) = QR(e(i1,i3));
	TEST(Q.representation == Tensor::Representation::Dense);
	TEST(R.representation == Tensor::Representation::Dense);
});


