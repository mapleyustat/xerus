// Xerus - A General Purpose Tensor Library
// Copyright (C) 2014-2015 Benjamin Huber and Sebastian Wolf. 
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


#include<xerus.h>

#include "../../include/xerus/misc/test.h"
using namespace xerus;

UNIT_TEST(algorithms, cg,
	std::random_device rd;
	std::mt19937_64 rnd(rd());
	std::uniform_real_distribution<value_t> dist (0.0, 1.0);
    
    Index i,j,k;
    
    TTTensor X = TTTensor::random({10,10,10}, {4,4}, rnd, dist);
	TTTensor B = TTTensor::random({10,10,10}, {2,2}, rnd, dist);
	TTOperator A = TTOperator::random({10,10,10,10,10,10}, {2,2}, rnd, dist);
	
	A(i/2,j/2) = A(k/2,i/2) * A(k/2,j/2);
// 	B(i&0) = A(i/2,j/2) * B(j&0);
	
	GeometricCGVariant cg(GeometricCG);
	cg.printProgress = true;
	cg.assumeSymmetricPositiveDefiniteOperator = true;
	cg.retraction = ALSRetractionI;
// 	cg.restartInterval = 20;
	
	cg(A, X, B);
	
	SteepestDescentVariant submanGrad(ALSRetractionII);
	submanGrad.printProgress = true;
	submanGrad.assumeSymmetricPositiveDefiniteOperator = true;
	
	submanGrad(A, X, B);
)


