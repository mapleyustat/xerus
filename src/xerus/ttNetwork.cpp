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

/**
* @file
* @brief Implementation of the TTNetwork class (and thus TTTensor and TTOperator).
*/

#include <algorithm>

#include <xerus/ttNetwork.h>

#include <xerus/misc/check.h>
#include <xerus/misc/math.h>
#include <xerus/misc/performanceAnalysis.h>

#include <xerus/basic.h>
#include <xerus/misc/basicArraySupport.h>
#include <xerus/index.h>
#include <xerus/tensor.h>
#include <xerus/ttStack.h>
#include <xerus/indexedTensorList.h>
#include <xerus/indexedTensorMoveable.h>

namespace xerus {
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Constructors - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	template<bool isOperator>
	TTNetwork<isOperator>::TTNetwork() : TensorNetwork(), cannonicalized(false) {}
	
	
	template<bool isOperator>
	TTNetwork<isOperator>::TTNetwork(const Tensor& _tensor, const double _eps, const size_t _maxRank) :
		TTNetwork(_tensor, _eps, std::vector<size_t>(_tensor.degree() == 0 ? 0 : _tensor.degree()/N-1, _maxRank)) {}
	
	
	template<bool isOperator>
	TTNetwork<isOperator>::TTNetwork(const size_t _degree) : TensorNetwork(ZeroNode::None), cannonicalized(true), corePosition(0) {
		REQUIRE(_degree%N==0, "illegal degree for TTOperator");
		const size_t numComponents = _degree/N;
		
		if (numComponents == 0) {
			nodes.emplace_back(std::unique_ptr<Tensor>(new Tensor()));
			return;
		}
		
		dimensions = std::vector<size_t>(_degree, 1);
		
		// ExternalLinks
		externalLinks.reserve(_degree);
		for (size_t i = 1; i <= numComponents; ++i) {
			externalLinks.emplace_back(i, 1, 1, false);
		}
		
		if (isOperator) {
			for (size_t i = 1; i <= numComponents; ++i) {
				externalLinks.emplace_back(i, 2, 1, false);
			}
		}
		
		std::vector<TensorNetwork::Link> neighbors;
		
		neighbors.emplace_back(1, 0, 1,false);
		
		nodes.emplace_back( std::unique_ptr<Tensor>(new Tensor(Tensor::ones({1}))), std::move(neighbors) );
		
		for (size_t i = 0; i < numComponents; ++i) {
			neighbors.clear();
			neighbors.emplace_back(i, i==0?0:N+1, 1, false);
			neighbors.emplace_back(-1, i, 1, true);
			if(isOperator) { neighbors.emplace_back(-1, i+numComponents, 1, true); }
			neighbors.emplace_back(i+2, 0, 1, false);
			
			nodes.emplace_back( std::unique_ptr<Tensor>(new Tensor(std::vector<size_t>(neighbors.size(), 1))), std::move(neighbors) );
		}
		
		neighbors.clear();
		neighbors.emplace_back(numComponents, N+1, 1, false);
		nodes.emplace_back( std::unique_ptr<Tensor>(new Tensor(Tensor::ones({1}))), std::move(neighbors));
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator>::TTNetwork(const Tensor& _tensor, const double _eps, const TensorNetwork::RankTuple& _maxRanks): TTNetwork(_tensor.degree()) {
		REQUIRE(_tensor.degree()%N==0, "Number of indicis must be even for TTOperator");
		REQUIRE(_eps >= 0 && _eps < 1, "_eps must be positive and smaller than one. " << _eps << " was given.");
		REQUIRE(_maxRanks.size() == num_ranks(), "We need " << num_ranks() <<" ranks but " << _maxRanks.size() << " where given");
		REQUIRE(!misc::contains(_maxRanks, 0ul), "Maximal ranks must be strictly positive. Here: " << _maxRanks);
		
		const size_t numComponents = degree()/N;
		
		if (_tensor.degree() == 0) {
			*nodes[0].tensorObject = _tensor;
			return;
		}
		
		dimensions = _tensor.dimensions;
		
		Tensor remains;
		if(isOperator) {
			std::vector<size_t> shuffle(_tensor.degree());
			for(size_t i = 0; i < numComponents; ++i) {
				shuffle[i] = 2*i;
				shuffle[numComponents + i] = 2*i+1; 
			}
			
			xerus::reshuffle(remains, _tensor, shuffle);
		} else {
			remains = _tensor;
		}
		
		// Add ghost dimensions used in the nodes
		std::vector<size_t> extDimensions;
		extDimensions.reserve(remains.degree()+2);
		extDimensions.emplace_back(1);
		extDimensions.insert(extDimensions.end(), remains.dimensions.begin(), remains.dimensions.end());
		extDimensions.emplace_back(1);
		remains.reinterpret_dimensions(extDimensions);
		
		
		Tensor singularValues, newNode;
		for(size_t position = numComponents-1; position > 0; --position) {
			calculate_svd(remains, singularValues, newNode, remains, 1+position*N, _maxRanks[position-1], _eps);
			
			set_component(position, std::move(newNode)); 
			newNode.reset();
			xerus::contract(remains, remains, false, singularValues, false, 1);
		}
		
		set_component(0, remains);
		assume_core_position(0);
	}
	

	template<bool isOperator>
	TTNetwork<isOperator>::TTNetwork(const TensorNetwork &_network, double _eps) : TTNetwork(Tensor(_network)) {
		LOG(warning, "Cast of arbitrary tensor network to TT not yet supported. Casting to Tensor first"); // TODO
	}

	
	template<bool isOperator>
	TTNetwork<isOperator> TTNetwork<isOperator>::ones(const std::vector<size_t>& _dimensions) {
		REQUIRE(_dimensions.size()%2 == 0, "Illegal number of dimensions for ttOperator");
		REQUIRE(!misc::contains(_dimensions, 0ul), "Trying to construct a TTTensor with dimension 0 is not possible.");
		
		if(_dimensions.empty()) {
			return TTNetwork(Tensor::ones({}));
		}
		
		TTNetwork result(_dimensions.size());
		const size_t numNodes = _dimensions.size()/N;
		
		std::vector<size_t> dimensions(isOperator ? 4 : 3, 1);
		for(size_t i = 0; i < numNodes; ++i) {
			dimensions[1] = _dimensions[i];
			if (isOperator) {
				dimensions[2] = _dimensions[i+numNodes];
			}
			result.set_component(i, Tensor::ones(dimensions));
		}
		result.cannonicalize_left();
		return result;
	}
	
	
	template<> template<>
	TTNetwork<true> TTNetwork<true>::identity(const std::vector<size_t>& _dimensions) {
		REQUIRE(_dimensions.size()%2==0, "Illegal number of dimensions for ttOperator");
		REQUIRE(!misc::contains(_dimensions, 0ul), "Trying to construct a TTTensor with dimension 0 is not possible.");
		
		if(_dimensions.empty()) {
			return TTNetwork(Tensor::ones({}));
		}
		
		const size_t numComponents = _dimensions.size()/N;
		
		TTNetwork result(_dimensions.size());
		
		std::vector<size_t> constructionVector(4, 1);
		for (size_t i = 0; i < numComponents; ++i) {
			constructionVector[1] = _dimensions[i];
			constructionVector[2] = _dimensions[i+numComponents];
			result.set_component(i, Tensor(constructionVector, [](const std::vector<size_t> &_idx){
				if (_idx[1] == _idx[2]) {
					return 1.0;
				} else {
					return 0.0;
				}
			}));
		}
		
		result.cannonicalize_left();
		return result;
	}
	
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Internal helper functions - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	
	#ifndef DISABLE_RUNTIME_CHECKS_
		template<bool isOperator>
		void TTNetwork<isOperator>::require_correct_format() const {
			require_valid_network(); // Network must at least be valid.
			
			const size_t numComponents = degree()/N;
			const size_t numNodes = degree() == 0 ? 1 : degree()/N + 2;
			REQUIRE(nodes.size() == numNodes, "Wrong number of nodes: " << nodes.size() << " expected " << numNodes << ".");
			REQUIRE(!cannonicalized || (degree() == 0 && corePosition == 0) || corePosition < numComponents, "Invalid corePosition: " << corePosition << " there are only " << numComponents << " components.");
			
			// Per external link
			for (size_t n = 0; n < externalLinks.size(); ++n) {
				const TensorNetwork::Link &l = externalLinks[n];
				REQUIRE(l.other == (n%numComponents)+1, "The " << n << "-th external link must point the the " << (n%numComponents) << "-th component (i.e. the " << (n%numComponents)+1 << "-th node) but does point to the " << l.other << "-th node.");
			}
			
			// Virtual nodes
			if(degree() > 0) {
				REQUIRE(nodes.front().degree() == 1, "The left virtual node must have degree 1, but has size " << nodes.front().degree());
				REQUIRE(nodes.front().neighbors[0].dimension == 1, "The left virtual node's single dimension must be 1, but is " << nodes.front().neighbors[0].dimension);
				REQUIRE(nodes.front().neighbors[0].other == 1, "The left virtual node's single link must be to node 1, but is towards node " << nodes.front().neighbors[0].other);
				REQUIRE(nodes.front().neighbors[0].indexPosition == 0, "The left virtual node's single link must link at indexPosition 0, but link at " << nodes.front().neighbors[0].indexPosition);
				REQUIRE(misc::hard_equal((*nodes.front().tensorObject)[0], 1.0), "The left virtual node's single entry must be 1.0, but it is " << (*nodes.front().tensorObject)[0]);
				REQUIRE(!nodes.front().tensorObject->has_factor(), "The left virtual node must no carry a non-trivial factor.");
				
				REQUIRE(nodes.back().degree() == 1, "The right virtual node must have degree 1, but has size " << nodes.back().degree());
				REQUIRE(nodes.back().neighbors[0].dimension == 1, "The right virtual node's single dimension must be 1, but is " << nodes.back().neighbors[0].dimension);
				REQUIRE(nodes.back().neighbors[0].other == numNodes-2, "The right virtual node's single link must be to node " << numNodes-2 << ", but is towards node " << nodes.back().neighbors[0].other);
				REQUIRE(nodes.back().neighbors[0].indexPosition == N+1, "The right virtual node's single link must link at indexPosition " << N+1 << ", but link at " << nodes.back().neighbors[0].indexPosition);
				REQUIRE(misc::hard_equal((*nodes.back().tensorObject)[0], 1.0), "The right virtual node's single entry must be 1.0, but it is " << (*nodes.back().tensorObject)[0]);
				REQUIRE(!nodes.back().tensorObject->has_factor(), "The right virtual node must no carry a non-trivial factor.");
			}
			
			// Per component
			for (size_t n = 0; n < numComponents; ++n) {
				const TensorNode& node = nodes[n+1];
				
				REQUIRE(!cannonicalized || n == corePosition || !node.tensorObject->has_factor(), "In cannonicalized TTNetworks only the core may carry a non-trivial factor. Violated by component " << n);
				
				REQUIRE(node.degree() == N+2, "Every TT-Component must have degree " << N+2 << ", but component " << n << " has degree " << node.degree());
				REQUIRE(!node.neighbors[0].external, "The first link of each TT-Component must not be external. Violated by component " << n);
				REQUIRE(node.neighbors[0].other == n, "The first link of each TT-Component must link to the previous node. Violated by component " << n << ", which instead links to node " << node.neighbors[0].other << " (expected " << n << ").");
				REQUIRE(node.neighbors[0].indexPosition == (n==0?0:N+1), "The first link of each TT-Component must link to the last last index of the previous node. Violated by component " << n << ", which instead links to index " << node.neighbors[0].indexPosition << " (expected " << (n==0?0:N+1) << ").");
				
				REQUIRE(node.neighbors[1].external, "The second link of each TT-Component must be external. Violated by component " << n << ".");
				REQUIRE(node.neighbors[1].indexPosition == n, "The second link of each TT-Component must link to the external dimension equal to the component position. Violated by component " << n << " which links at " << node.neighbors[1].indexPosition);
				REQUIRE(!isOperator || node.neighbors[2].external, "The third link of each TTO-Component must be external. Violated by component " << n << ".");
				REQUIRE(!isOperator || node.neighbors[2].indexPosition == numComponents+n, "The third link of each TTO-Component must link to the external dimension equal to the component position + numComponents. Violated by component " << n << " which links at " << node.neighbors[2].indexPosition << " (expected " << numComponents+n << ").");
				
				REQUIRE(!node.neighbors.back().external, "The last link of each TT-Component must not be external. Violated by component " << n);
				REQUIRE(node.neighbors.back().other == n+2, "The last link of each TT-Component must link to the next node. Violated by component " << n << ", which instead links to node " << node.neighbors.back().other << " (expected " << n+2 << ").");
				REQUIRE(node.neighbors.back().indexPosition == 0, "The last link of each TT-Component must link to the first index of the next node. Violated by component " << n << ", which instead links to index " << node.neighbors.back().indexPosition << " (expected 0).");
			}
		}
	#else
		template<bool isOperator>
		void TTNetwork<isOperator>::require_correct_format() const { }
	#endif
	
	
	template<bool isOperator>
	bool TTNetwork<isOperator>::exceeds_maximal_ranks() const {
		for (size_t i = 0; i < degree()/N; ++i) {
			const Tensor& comp = get_component(i);
			size_t extDim = comp.dimensions[1];
			if (isOperator) {
				extDim *= comp.dimensions[2];
			}
			if (comp.dimensions.front() > extDim * comp.dimensions.back() || comp.dimensions.back() > extDim * comp.dimensions.front()) {
				return true;
			}
		}
		return false;
	}
	
	
	template<bool isOperator>
	size_t TTNetwork<isOperator>::num_ranks() const {
		return degree() == 0 ? 0 : degree()/N-1;
	}
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Miscellaneous - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	
	template<bool isOperator>
	std::vector<size_t> TTNetwork<isOperator>::reduce_to_maximal_ranks(std::vector<size_t> _ranks, const std::vector<size_t>& _dimensions) {
		const size_t numComponents = _dimensions.size()/N;
		REQUIRE(numComponents == _ranks.size()+1, "Invalid number of ranks ("<<_ranks.size()<<") or dimensions ("<<_dimensions.size()<<") given.");
		
		// Left to right sweep
		size_t currMax = 1;
		for (size_t i = 0; i+1 < numComponents; ++i) {
			currMax *= _dimensions[i];
			if (isOperator) { currMax *= _dimensions[numComponents+i]; }
			
			if (currMax < _ranks[i]) { 
				_ranks[i] = currMax;
			} else {
				currMax = _ranks[i];
			}
		}
	
		// Right to left sweep
		currMax = 1;
		for (size_t i = 1; i < numComponents; ++i) {
			currMax *= _dimensions[numComponents-i];
			if (isOperator) { currMax *= _dimensions[2*numComponents-i]; }
			
			if (currMax < _ranks[numComponents-i-1]) {
				_ranks[numComponents-i-1] = currMax;
			} else {
				currMax = _ranks[numComponents-i-1];
			}
		}
		
		return _ranks;
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::fix_slate(const size_t _dimension, const size_t _slatePosition) {
		REQUIRE(!isOperator, "fix_slate(), does not work for TTOperators, if applicable cast to TensorNetwork first");
		TensorNetwork::fix_slate(_dimension, _slatePosition);
	}
	
	
	template<bool isOperator>
	Tensor& TTNetwork<isOperator>::component(const size_t _idx) {
		REQUIRE(_idx == 0 || _idx < degree()/N, "Illegal index " << _idx <<" in TTNetwork::component.");
		return *nodes[degree() == 0 ? 0 : _idx+1].tensorObject;
	}
	
	
	template<bool isOperator>
	const Tensor& TTNetwork<isOperator>::get_component(const size_t _idx) const {
		REQUIRE(_idx == 0 || _idx < degree()/N, "Illegal index " << _idx <<" in TTNetwork::get_component.");
		return *nodes[degree() == 0 ? 0 : _idx+1].tensorObject;
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::set_component(const size_t _idx, Tensor _T) {
		if(degree() == 0) {
			REQUIRE(_idx == 0, "Illegal index " << _idx <<" in TTNetwork::set_component");
			REQUIRE(_T.degree() == 0, "Component of degree zero TTNetwork must have degree zero. Given: " << _T.degree());
			*nodes[0].tensorObject = std::move(_T);
		} else {
			REQUIRE(_idx < degree()/N, "Illegal index " << _idx <<" in TTNetwork::set_component");
			REQUIRE(_T.degree() == N+2, "Component must have degree 3 (TTTensor) or 4 (TTOperator). Given: " << _T.degree());
			
			TensorNode& currNode = nodes[_idx+1];
			*currNode.tensorObject = std::move(_T);
			for (size_t i = 0; i < N+2; ++i) {
				currNode.neighbors[i].dimension = currNode.tensorObject->dimensions[i];
				if (currNode.neighbors[i].external) {
					externalLinks[currNode.neighbors[i].indexPosition].dimension = currNode.tensorObject->dimensions[i];
					dimensions[currNode.neighbors[i].indexPosition] = currNode.tensorObject->dimensions[i];
				}
			}
		}
		
		cannonicalized = cannonicalized && (corePosition == _idx);
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator> TTNetwork<isOperator>::dyadic_product(const TTNetwork<isOperator> &_lhs, const TTNetwork<isOperator> &_rhs) {
		_lhs.require_correct_format();
		_rhs.require_correct_format();
		
		if (_lhs.degree() == 0) {
			TTNetwork result(_rhs);
			result *= _lhs[0];
			return result;
		}
		
		TTNetwork result(_lhs);
		if (_rhs.degree() == 0) {
			result *= _rhs[0];
			return result;
		}
		
		const size_t lhsNumComponents = _lhs.degree()/N;
		const size_t rhsNumComponents = _rhs.degree()/N;
		
		// Add all nodes of rhs and fix neighbor relations
		result.nodes.pop_back();
		result.nodes.reserve(_lhs.degree()+_rhs.degree()+2);
		for (size_t i = 1; i < _rhs.nodes.size(); ++i) {
			result.nodes.emplace_back(_rhs.nodes[i]);
			for (TensorNetwork::Link &l : result.nodes.back().neighbors) {
				if (l.external) {
					if (l.indexPosition < rhsNumComponents) {
						l.indexPosition += lhsNumComponents;
					} else {
						l.indexPosition += 2*lhsNumComponents;
					}
				} else {
					if (l.other==0) {
						l.indexPosition = N+1;
					}
					l.other += lhsNumComponents;
				}
			}
		}
		
		// Add all external indices of rhs
		if(isOperator) {
			result.externalLinks.resize(lhsNumComponents);
			result.dimensions.resize(lhsNumComponents);
		}
		result.externalLinks.reserve(_lhs.degree()+_rhs.degree());
		result.dimensions.reserve(_lhs.degree()+_rhs.degree());
		
		for (size_t i = 0; i < rhsNumComponents; ++i) {
			const size_t d = _rhs.dimensions[i];
			result.externalLinks.emplace_back(lhsNumComponents+i+1, 1, d, false);
			result.dimensions.push_back(d);
		}
		
		if (isOperator) {
			for (size_t i = 0; i < lhsNumComponents; ++i) {
				const size_t d = _lhs.dimensions[i];
				result.externalLinks.emplace_back(i+1, 2, d, false);
				result.dimensions.push_back(d);
			}
			for (size_t i = 0; i < rhsNumComponents; ++i) {
				const size_t d = _rhs.dimensions[i];
				result.externalLinks.emplace_back(lhsNumComponents+i+1, 2, d, false);
				result.dimensions.push_back(d);
			}
		}
		
		if (_lhs.cannonicalized && _rhs.cannonicalized) {
			if (_lhs.corePosition == 0 && _rhs.corePosition == 0) {
				result.cannonicalized = true;
				result.corePosition = lhsNumComponents;
				result.move_core(0);
			}
			if (_lhs.corePosition == lhsNumComponents-1 && _rhs.corePosition == rhsNumComponents-1) {
				result.cannonicalized = true;
				result.corePosition = lhsNumComponents-1;
				result.move_core(lhsNumComponents + rhsNumComponents -1);
			}
		} else {
			result.cannonicalized = false;
		}
		
		result.require_correct_format();
		return result;
	}
	
	template<bool isOperator>
	TTNetwork<isOperator> TTNetwork<isOperator>::dyadic_product(const std::vector<std::reference_wrapper<TTNetwork<isOperator>>>& _tensors) {
		if (_tensors.empty()) { return TTNetwork(); }
		
		TTNetwork result(_tensors.back());
		// construct dyadic products right to left as default cannonicalization is left
		for (size_t i = _tensors.size()-1; i > 0; --i) {
			REQUIRE_TEST;
			result = dyadic_product(_tensors[i-1], result);
		}
		return result;
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator> TTNetwork<isOperator>::entrywise_product(const TTNetwork &_A, const TTNetwork &_B) {
		REQUIRE(_A.dimensions == _B.dimensions, "Entrywise_product ill-defined for non equal dimensions");
		
		if(_A.degree() == 0) {
			TTNetwork result(_A);
			result *= _B[0];
			return result;
		}
		
		TTNetwork result(_A.degree());
		
		const size_t numComponents = _A.degree() / N;
		
		std::unique_ptr<Tensor> newComponent;
		for (size_t i = 0; i < numComponents; ++i) {
			//TODO sparse TT
			REQUIRE(!_A.get_component(i).is_sparse(), "sparse tensors in TT not allowed");
			REQUIRE(!_B.get_component(i).is_sparse(), "sparse tensors in TT not allowed");
			const Tensor& componentA = _A.get_component(i);
			const Tensor& componentB = _B.get_component(i);
			size_t externalDim;
			Tensor::Representation newRep = componentA.is_sparse() && componentB.is_sparse() ? Tensor::Representation::Sparse : Tensor::Representation::Dense;
			REQUIRE(newRep == Tensor::Representation::Dense, "entrywise product of sparse TT not yet implemented!");
			if (isOperator) {
				newComponent.reset(new Tensor({componentA.dimensions.front()*componentB.dimensions.front(), 
											componentA.dimensions[1], componentA.dimensions[2], 
											componentA.dimensions.back()*componentB.dimensions.back()   }, newRep));
				externalDim = componentA.dimensions[1] * componentA.dimensions[2];
			} else {
				newComponent.reset(new Tensor({componentA.dimensions.front()*componentB.dimensions.front(), 
											componentA.dimensions[1], 
											componentA.dimensions.back()*componentB.dimensions.back()   }, newRep));
				externalDim = componentA.dimensions[1];
			}
			size_t offsetA = 0, offsetB = 0, offsetResult = 0;
			const size_t stepsize = componentB.dimensions.back();
			for (size_t r1 = 0; r1 < componentA.dimensions.front(); ++r1) {
				for (size_t s1 = 0; s1 < componentB.dimensions.front(); ++s1) {
					offsetA = r1 * externalDim * componentA.dimensions.back();
					for (size_t n = 0; n < externalDim; ++n) {
						for (size_t r2 = 0; r2 < componentA.dimensions.back(); ++r2) {
							misc::copy_scaled(newComponent->get_unsanitized_dense_data()+offsetResult, componentB.factor*componentA.factor*componentA.get_unsanitized_dense_data()[offsetA], componentB.get_unsanitized_dense_data()+offsetB, stepsize);
							offsetResult += stepsize;
							offsetA += 1;
						}
						offsetB += stepsize;
					}
				}
				offsetB = 0;
			}
			result.set_component(i, std::move(*newComponent));
		}
		
		result.require_correct_format();
		
		if (_A.cannonicalized) {
			result.move_core(_A.corePosition);
		}
		return result;
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::entrywise_square() {
		const size_t numComponents = degree() / N;
		const bool cannonicalizedBefore = cannonicalized;
		
		if(degree() == 0) {
			(*nodes[0].tensorObject)[0] *= (*nodes[0].tensorObject)[0];
		} else if ( degree() <= 2 ) {
			for (size_t i = 0; i < numComponents; ++i) {
				const Tensor& currComp = get_component(i);
				const size_t newLeftRank = currComp.dimensions.front()*(currComp.dimensions.front()+1)/2;
				const size_t newRightRank = currComp.dimensions.back()*(currComp.dimensions.back()+1)/2;
				
				Tensor newComponent(isOperator ? 
					std::vector<size_t>({newLeftRank, currComp.dimensions[1], currComp.dimensions[2], newRightRank})
					: std::vector<size_t>({newLeftRank,  currComp.dimensions[1], newRightRank}), Tensor::Representation::Dense, Tensor::Initialisation::None );
				
				const size_t externalDim = isOperator ? currComp.dimensions[1] * currComp.dimensions[2] : currComp.dimensions[1];
				const size_t oldLeftStep = externalDim*currComp.dimensions.back();
				const size_t oldExtStep = currComp.dimensions.back();
				
				size_t newPos = 0;
				for (size_t r1 = 0; r1 < currComp.dimensions.front(); ++r1) {
					for (size_t r2 = 0; r2 <= r1; ++r2) {
						for (size_t n = 0; n < externalDim; ++n) {
							for (size_t s1 = 0; s1 < currComp.dimensions.back(); ++s1) {
								for (size_t s2 = 0; s2 <= s1; ++s2) {
									newComponent[newPos++] = (s1 == s2 ? 1.0 : 2.0) * currComp[r1*oldLeftStep + n*oldExtStep + s1] * currComp[r2*oldLeftStep + n*oldExtStep + s2];
								}
							}
						}
					}
				}
				set_component(i, std::move(newComponent));
			}
		} else {
			for (size_t i = 0; i < numComponents; ++i) {
				REQUIRE(!get_component(i).is_sparse(), "sparse tensors in TT not allowed");
				const Tensor& currComp = get_component(i);
				const size_t newLeftRank = currComp.dimensions.front()*currComp.dimensions.front();
				const size_t newRightRank = currComp.dimensions.back()*currComp.dimensions.back();
				
				Tensor::Representation newRep = currComp.representation;
				REQUIRE(newRep == Tensor::Representation::Dense, "entrywise product of sparse TT not yet implemented!");
				
				Tensor newComponent(isOperator ? 
					std::vector<size_t>({newLeftRank, currComp.dimensions[1], currComp.dimensions[2], newRightRank})
					: std::vector<size_t>({newLeftRank,  currComp.dimensions[1], newRightRank}), newRep, Tensor::Initialisation::None );
				
				const size_t externalDim = isOperator ? currComp.dimensions[1] * currComp.dimensions[2] : currComp.dimensions[1];
				const size_t oldLeftStep = externalDim*currComp.dimensions.back();
				const size_t oldExtStep = currComp.dimensions.back();
				
				size_t newPos = 0;
				for (size_t r1 = 0; r1 < currComp.dimensions.front(); ++r1) {
					for (size_t r2 = 0; r2 < currComp.dimensions.front(); ++r2) {
						for (size_t n = 0; n < externalDim; ++n) {
							for (size_t s1 = 0; s1 < currComp.dimensions.back(); ++s1) {
								misc::copy_scaled(newComponent.get_unsanitized_dense_data()+newPos, currComp.factor * currComp[r1*oldLeftStep + n*oldExtStep + s1], currComp.get_unsanitized_dense_data()+ r2*oldLeftStep + n*oldExtStep, currComp.dimensions.back());
								newPos += currComp.dimensions.back();
							}
						}
					}
				}
				set_component(i, std::move(newComponent));
			}
		}
		
		// Restore cannonicalization
		if(cannonicalizedBefore) {
			move_core(corePosition);
		}
	}
	
	template<bool isOperator>
	std::pair<TensorNetwork, TensorNetwork> TTNetwork<isOperator>::chop(const size_t _position) const {
		require_correct_format();
		
		const size_t numComponents = degree()/N;
		REQUIRE(_position < numComponents, "Can't split a " << numComponents << " component TTNetwork at position " << _position);
		
		// Create the resulting TNs
		TensorNetwork left(ZeroNode::None);
		TensorNetwork right(ZeroNode::None);
		
		left.nodes.push_back(nodes[0]);
		for (size_t i = 0; i < _position; ++i) {
			left.dimensions.push_back(dimensions[i]);
			left.externalLinks.push_back(externalLinks[i]);
			left.nodes.push_back(nodes[i+1]);
		}
		if(isOperator) {
			for(size_t i = 0; i < _position; ++i) {
				left.dimensions.push_back(dimensions[i+numComponents]);
				left.externalLinks.push_back(externalLinks[i+numComponents]);
			}
		}
		left.dimensions.push_back(left.nodes.back().neighbors.back().dimension);
		left.externalLinks.emplace_back(_position, _position==0?0:N+1, left.nodes.back().neighbors.back().dimension , false);
		left.nodes.back().neighbors.back().external = true;
		left.nodes.back().neighbors.back().indexPosition = isOperator ? 2*_position-1 : _position;
		
		right.dimensions.push_back(nodes[_position+2].neighbors.front().dimension);
		right.externalLinks.emplace_back(_position+2, 0, nodes[_position+2].neighbors.front().dimension , false); // NOTE other will be corrected to 0 in the following steps

		for(size_t i = _position+1; i < numComponents; ++i) {
			right.dimensions.push_back(dimensions[i]);
			right.externalLinks.push_back(externalLinks[i]);
			right.nodes.push_back(nodes[i+1]);
		}
		if(isOperator) {
			for(size_t i = _position+1; i < numComponents+1; ++i) {
				right.dimensions.push_back(dimensions[i+numComponents]);
				right.externalLinks.push_back(externalLinks[i+numComponents]);
			}
		}
		// The last node
		right.nodes.push_back(nodes.back());
		
		right.nodes.front().neighbors.front().external = true;
		right.nodes.front().neighbors.front().indexPosition = _position; // NOTE indexPosition will be corrected to 0 in the following steps
		
		// Account for the fact that the first _position+2 nodes do not exist
		for(TensorNetwork::Link& link : right.externalLinks) {
			link.other -= _position+2;
		}
		
		for(TensorNode& node : right.nodes) {
			for(TensorNetwork::Link& link : node.neighbors) {
				if(link.external) {
					link.indexPosition -= _position;
				} else {
					link.other -= _position+2;
				}
			}
		}
		
		return std::pair<TensorNetwork, TensorNetwork>(std::move(left), std::move(right));
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::move_core(const size_t _position, const bool _keepRank) {
		const size_t numComponents = degree()/N;
		REQUIRE(_position < numComponents || (_position == 0 && degree() == 0), "Illegal position for core chosen.");
		require_correct_format();
		
		if(cannonicalized) {
			// Move right?
			for (size_t n = corePosition; n < _position; ++n) {
				transfer_core(n+1, n+2, !_keepRank);
			}
			
			// Move left?
			for (size_t n = corePosition; n > _position; --n) {
				transfer_core(n+1, n, !_keepRank);
			}
		} else {
			// Move right?
			for (size_t n = 0; n < _position; ++n) {
				transfer_core(n+1, n+2, !_keepRank);
			}
			
			// Move left?
			for (size_t n = numComponents; n > _position+1; --n) {
				transfer_core(n, n-1, !_keepRank);
			}
		}
		
		while (exceeds_maximal_ranks()) {
			// Move left from given CorePosition
			for (size_t n = _position; n > 0; --n) {
				transfer_core(n+1, n, !_keepRank);
			}
			
			// Move to the most right
			for (size_t n = 0; n+1 < numComponents; ++n) {
				transfer_core(n+1, n+2, !_keepRank);
			}
			
			// Move back left to given CorePosition
			for (size_t n = numComponents; n > _position+1; --n) {
				transfer_core(n, n-1, !_keepRank);
			}
		}
		
		cannonicalized = true;
		corePosition = _position;
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::cannonicalize_left() {
		move_core(0);
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::cannonicalize_right() {
		move_core(degree() == 0 ? 0 : degree()/N-1);
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::round(const std::vector<size_t>& _maxRanks, const double _eps) {
		require_correct_format();
		const size_t numComponents = degree()/N;
		REQUIRE(_eps < 1, "_eps must be smaller than one. " << _eps << " was given.");
		REQUIRE(_maxRanks.size()+1 == numComponents || (_maxRanks.empty() && numComponents == 0), "There must be exactly degree/N-1 maxRanks. Here " << _maxRanks.size() << " instead of " << numComponents-1 << " are given.");
		REQUIRE(!misc::contains(_maxRanks, 0ul), "Trying to round a TTTensor to rank 0 is not possible.");
		
		const bool initialCanonicalization = cannonicalized;
		const size_t initialCorePosition = corePosition;
		
		cannonicalize_right();
		
		for(size_t i = 0; i+1 < numComponents; ++i) {
			round_edge(numComponents-i, numComponents-i-1, _maxRanks[numComponents-i-2], _eps, 0.0, false);
		}
		
		assume_core_position(0);
		
		if(initialCanonicalization) {
			move_core(initialCorePosition);
		}
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::round(const size_t _maxRank) {
		round(std::vector<size_t>(num_ranks(), _maxRank), EPSILON);
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::round(const int _maxRank) {
		REQUIRE( _maxRank > 0, "MaxRank must be positive");
		round(size_t(_maxRank));
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::round(const value_t _eps) {
		round(std::vector<size_t>(num_ranks(), std::numeric_limits<size_t>::max()), _eps);
	}

	
	template<bool isOperator>
	void TTNetwork<isOperator>::soft_threshold(const std::vector<double> &_taus, const bool _preventZero) {
		const size_t numComponents = degree()/N;
		REQUIRE(_taus.size()+1 == numComponents || (_taus.empty() && numComponents == 0), "There must be exactly degree/N-1 taus. Here " << _taus.size() << " instead of " << numComponents-1 << " are given.");
		require_correct_format();
		
		const bool initialCanonicalization = cannonicalized;
		const size_t initialCorePosition = corePosition;
		
		cannonicalize_right();
		
		for(size_t i = 0; i+1 < numComponents; ++i) {
			round_edge(numComponents-i, numComponents-i-1, std::numeric_limits<size_t>::max(), 0.0, _taus[i], true);
		}
		
		assume_core_position(0);
		
		if(initialCanonicalization) {
			move_core(initialCorePosition);
		}
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::soft_threshold(const double _tau, const bool _preventZero) {
		soft_threshold(std::vector<double>(num_ranks(), _tau), _preventZero);
	}
	

	template<bool isOperator>
	std::vector<size_t> TTNetwork<isOperator>::ranks() const {
		std::vector<size_t> res;
		res.reserve(num_ranks());
		for (size_t n = 1; n+2 < nodes.size(); ++n) {
			res.push_back(nodes[n].neighbors.back().dimension);
		}
		return res;
	}
	
	
	template<bool isOperator>
	size_t TTNetwork<isOperator>::rank(const size_t _i) const {
		REQUIRE(_i+1 < degree()/N, "Requested illegal rank " << _i);
		return nodes[_i+1].neighbors.back().dimension;
	}
	
	
	template<bool isOperator>
	size_t TTNetwork<isOperator>::datasize() const {
		size_t result = 0;
		for (const TensorNode& node : nodes) {
			result += node.tensorObject->size;
		}
		return result;
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::assume_core_position(const size_t _pos) {
		REQUIRE(_pos < degree()/N || (degree() == 0 && _pos == 0), "Invalid core position.");
		corePosition = _pos;
		cannonicalized = true;
	}
	
	
	template<bool isOperator>
	TensorNetwork* TTNetwork<isOperator>::get_copy() const {
		return new TTNetwork(*this);
	}
	
	template<bool isOperator>
	void TTNetwork<isOperator>::contract_unconnected_subnetworks() {
		if(degree() == 0) {
			std::set<size_t> all;
			for(size_t i = 0; i < nodes.size(); ++i) { all.emplace_hint(all.end(), i); }
			contract(all);
			cannonicalized = false;
		} else {
			REQUIRE(nodes.size() > 2, "Invalid TTNetwork");
			const size_t numComponents = nodes.size()-2;
			
			for(size_t i = 0; i+1 < numComponents; ++i) {
				if(nodes[i+1].degree() == 2) {
						// If we are the core, everything is fine, we contract ourself to the next node, then get removed and the corePositions stays. If the next Node is the core, we have to change the corePosition to ours, because we will be removed. In all other cases cannonicalization is destroyed.
						if(corePosition == i+1) { corePosition = i; }
						else if(corePosition != i) { cannonicalized = false; }
						contract(i+1, i+2);
				}
			}
			
			// Extra treatment for last component to avoid contraction to the pseudo-node.
			if(nodes[numComponents].degree() == 2) {
				if(corePosition == numComponents-1) { corePosition = numComponents-2; }
				else if(corePosition != numComponents-2) { cannonicalized = false; }
				contract(numComponents-1, numComponents);
			}
		}
		
		REQUIRE(corePosition < degree() || !cannonicalized, "Woot");
		
		sanitize();
	}
	
	
	template<bool isOperator>
	value_t TTNetwork<isOperator>::frob_norm() const {
		require_correct_format();
		if (cannonicalized) {
			return get_component(corePosition).frob_norm();
		} else {
			const Index i;
			return std::sqrt(value_t((*this)(i&0)*(*this)(i&0)));
		}
	}
	
	
	template<bool isOperator>
	size_t TTNetwork<isOperator>::find_largest_entry(const double _accuracy, const value_t _lowerBound) const {
		require_correct_format();
		
		// There is actual work to be done
		if(misc::sum(ranks()) >= degree()) {
			const double alpha = _accuracy;
			
			TTNetwork X = *this;
			X.round(1ul);
			double Xn = std::max(operator[](X.find_largest_entry(0.0, 0.0)), _lowerBound);
			double tau = (1-alpha)*alpha*Xn*Xn/(2.0*double(degree()-1));
			
			X = *this;
			while(misc::sum(X.ranks()) >= degree()) {
				X.entrywise_square();
				
				X.soft_threshold(tau, true);
				
				TTNetwork Y = X;
				Y.round(1);
				const size_t yMaxPos = Y.find_largest_entry(0.0, 0.0);
				
				Xn = std::max(X[yMaxPos], (1-(1-alpha)*alpha/2.0)*Xn*Xn);
				
				const double fNorm = X.frob_norm();
				Xn /= fNorm;
				X /= fNorm;
				tau = (1-alpha)*alpha*Xn*Xn/(2.0*double(degree()-1));
			}
			return X.find_largest_entry(0.0, 0.0);
			
		// We are already rank one
		} else {
			const size_t numComponents = degree()/N;
			size_t position = 0;
			size_t factor = misc::product(dimensions);
			for(size_t c = 0; c < numComponents; ++c) {
				const size_t localSize = isOperator ? dimensions[c]*dimensions[numComponents+c] : dimensions[c];
				factor /= localSize;
				
				size_t maxPos = 0;
				for(size_t i = 1; i < localSize; ++i) {
					if(std::abs(get_component(c)[i]) > std::abs(get_component(c)[maxPos])) {
						maxPos = i;
					}
				}
				position += maxPos*factor;
			}
			return position;
		}
	}
	
	
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - -  Basic arithmetics - - - - - - - - - - - - - - - - - - - - - - - - - - */
	
	template<bool isOperator>
	TTNetwork<isOperator>& TTNetwork<isOperator>::operator+=(const TTNetwork<isOperator>& _other) {
		REQUIRE(dimensions == _other.dimensions, "The dimensions in TT sum must coincide. Given " << dimensions << " vs " << _other.dimensions);
		require_correct_format();
		
		const size_t numComponents = degree()/N;
		
		const bool initialCanonicalization = cannonicalized;
		const size_t initialCorePosition = corePosition;
		
		if (numComponents <= 1) {
			component(0) += _other.get_component(0);
			return *this;
		}
		
		
		for(size_t position = 0; position < numComponents; ++position) {
			// Get current components
			const Tensor& myComponent = get_component(position);
			const Tensor& otherComponent = _other.get_component(position);
			
			// Structure has to be (for degree 4)
			// (L1 R1) * ( L2 0  ) * ( L3 0  ) * ( L4 )
			// 			 ( 0  R2 )   ( 0  R3 )   ( R4 )
			
			// Create a Tensor for the result
			std::vector<size_t> nxtDimensions;
			if (position == 0) { 
				nxtDimensions.emplace_back(1);
			} else {
				nxtDimensions.emplace_back(myComponent.dimensions.front()+otherComponent.dimensions.front());
			}
			nxtDimensions.emplace_back(myComponent.dimensions[1]);
			if (isOperator) { nxtDimensions.emplace_back(myComponent.dimensions[2]); }
			if (position == numComponents-1) {
				nxtDimensions.emplace_back(1);
			} else {
				nxtDimensions.emplace_back(myComponent.dimensions.back()+otherComponent.dimensions.back());
			}
			
			Tensor::Representation newRep = myComponent.is_sparse() || otherComponent.is_sparse() ? Tensor::Representation::Sparse : Tensor::Representation::Dense;
			std::unique_ptr<Tensor> newComponent(new Tensor(std::move(nxtDimensions), newRep));
			REQUIRE(newRep == Tensor::Representation::Dense, "ie");
			value_t * const componentData = static_cast<Tensor*>(newComponent.get())->get_unsanitized_dense_data();
			
			const size_t leftIdxOffset = newComponent->size/newComponent->dimensions.front();
			const size_t extIdxOffset = newComponent->dimensions.back();
			const size_t myLeftIdxOffset = myComponent.size/myComponent.dimensions.front();
			const size_t myExtIdxOffset = myComponent.dimensions.back();
			const size_t otherLeftIdxOffset = otherComponent.size/otherComponent.dimensions.front();
			const size_t otherExtIdxOffset = otherComponent.dimensions.back();
			const size_t otherGeneralOffset = (position == 0 ? 0 : myComponent.dimensions.front()*leftIdxOffset) + (position == numComponents-1 ? 0 : myComponent.dimensions.back());
			const size_t extDimSize = myComponent.dimensions[1] * (isOperator? myComponent.dimensions[2] : 1);
			
			// Copy own Tensor into place
			if (!initialCanonicalization || position == initialCorePosition) {
				for(size_t leftIdx = 0; leftIdx < myComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied in one piece
						misc::copy_scaled(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset, 
												myComponent.factor, 
												myComponent.get_unsanitized_dense_data() + leftIdx*myLeftIdxOffset + extIdx*myExtIdxOffset, 
												myComponent.dimensions.back());
					}
				}
			} else {
				REQUIRE(!myComponent.has_factor(), "Only Core node is allowed to have a factor");
				for(size_t leftIdx = 0; leftIdx < myComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied as one piece
						misc::copy(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset, 
										myComponent.get_unsanitized_dense_data() + leftIdx*myLeftIdxOffset + extIdx*myExtIdxOffset, 
										myComponent.dimensions.back());
					}
				}
			}
			
			
			// Copy other Tensor into place
			if(!_other.cannonicalized || position == _other.corePosition) {
				for(size_t leftIdx = 0; leftIdx < otherComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied as one piece
						misc::copy_scaled(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset + otherGeneralOffset, 
												otherComponent.factor, 
												otherComponent.get_unsanitized_dense_data() + leftIdx*otherLeftIdxOffset + extIdx*otherExtIdxOffset, 
												otherComponent.dimensions.back());
					}
				}
			} else {
				REQUIRE(!otherComponent.has_factor(), "Only Core node is allowed to have a factor");
				for(size_t leftIdx = 0; leftIdx < otherComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied as one piece
						misc::copy(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset + otherGeneralOffset, 
										otherComponent.get_unsanitized_dense_data() + leftIdx*otherLeftIdxOffset + extIdx*otherExtIdxOffset, 
										otherComponent.dimensions.back());
					}
				}
			}
			
			set_component(position, std::move(*newComponent));
		}
		
		if(initialCanonicalization) {
			move_core(initialCorePosition);
		}
		
		return *this;
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator>& TTNetwork<isOperator>::operator-=(const TTNetwork<isOperator>& _other) {
		operator*=(-1.0);
		operator+=(_other);
		operator*=(-1.0);
		return *this;
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::operator*=(const value_t _factor) {
		REQUIRE(nodes.size() > 0, "There must not be a TTNetwork without any node");
		
		if(cannonicalized) {
			component(corePosition) *= _factor;
		} else {
			component(0) *= _factor;
		}
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::operator/=(const value_t _divisor) {
		operator*=(1/_divisor);
	}
	
	
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Operator specializations - - - - - - - - - - - - - - - - - - - - - - - - - - */
	
	
	template<bool isOperator>
	bool TTNetwork<isOperator>::specialized_contraction_f(std::unique_ptr<internal::IndexedTensorMoveable<TensorNetwork>>& _out, internal::IndexedTensorReadOnly<TensorNetwork>&& _me, internal::IndexedTensorReadOnly<TensorNetwork>&& _other) {
		REQUIRE(!_out->tensorObject, "Internal Error.");
		
		// Only TTOperators construct stacks, so no specialized contractions for TTTensors
		if(!isOperator) { return false; }
		
		_me.assign_indices();
		_other.assign_indices();
		
		const TTNetwork* const meTT = dynamic_cast<const TTNetwork*>(_me.tensorObjectReadOnly);
		const internal::TTStack<true>* const meTTStack = dynamic_cast<const internal::TTStack<true>*>(_me.tensorObjectReadOnly);
		REQUIRE(meTT || meTTStack, "Internal Error.");
		
		const TTTensor* const otherTT = dynamic_cast<const TTTensor*>(_other.tensorObjectReadOnly);
		const internal::TTStack<false>* const otherTTStack = dynamic_cast<const internal::TTStack<false>*>(_other.tensorObjectReadOnly);
		const TTOperator* const otherTTO = dynamic_cast<const TTOperator*>(_other.tensorObjectReadOnly);
		const internal::TTStack<true>* const otherTTOStack = dynamic_cast<const internal::TTStack<true>*>(_other.tensorObjectReadOnly);
		
		if (!otherTT && !otherTTStack && !otherTTO && !otherTTOStack) {
			return false;
		}
		
		bool cannoAtTheEnd = false;
		size_t coreAtTheEnd = 0;
		if (meTT) {
			cannoAtTheEnd = meTT->cannonicalized;
			coreAtTheEnd = meTT->corePosition;
		} else {
			cannoAtTheEnd = meTTStack->cannonicalization_required;
			coreAtTheEnd = meTTStack->futureCorePosition;
		}
		
		
		// TODO profiler should warn if other->corePosition is not identical to coreAtTheEnd
		
		// Determine my first half and second half of indices
		std::vector<Index>::iterator midIndexItr = _me.indices.begin();
		size_t spanSum = 0;
		while (spanSum < _me.degree() / 2) {
			REQUIRE(midIndexItr != _me.indices.end(), "Internal Error.");
			spanSum += midIndexItr->span;
			++midIndexItr;
		}
		if (spanSum > _me.degree() / 2) {
			return false; // an index spanned some links of the left and some of the right side
		}
		
		if (otherTT) {
			// ensure fitting indices
			if (std::equal(_me.indices.begin(), midIndexItr, _other.indices.begin()) || std::equal(midIndexItr, _me.indices.end(), _other.indices.begin())) {
				TensorNetwork *res = new internal::TTStack<false>(cannoAtTheEnd, coreAtTheEnd);
				*res = *_me.tensorObjectReadOnly;
				_out.reset(new internal::IndexedTensorMoveable<TensorNetwork>(res, _me.indices));
				TensorNetwork::add_network_to_network(std::move(*_out), std::move(_other));
				return true;
			} else {
				return false;
			}
		} else { // other is operator or operator stack
			// determine other middle index
			auto otherMidIndexItr = _other.indices.begin();
			spanSum = 0;
			while (spanSum < _other.degree() / 2) {
				REQUIRE(otherMidIndexItr != _other.indices.end(), "Internal Error.");
				spanSum += otherMidIndexItr->span;
				++otherMidIndexItr;
			}
			if (spanSum > _other.degree() / 2) {
				return false; // an index spanned some links of the left and some of the right side
			}
			// or indices in fitting order to contract the TTOs
			if (   std::equal(_me.indices.begin(), midIndexItr, _other.indices.begin()) 
				|| std::equal(midIndexItr, _me.indices.end(), _other.indices.begin())
				|| std::equal(_me.indices.begin(), midIndexItr, otherMidIndexItr) 
				|| std::equal(midIndexItr, _me.indices.end(), otherMidIndexItr)) 
			{
				TensorNetwork *res = new internal::TTStack<true>(cannoAtTheEnd, coreAtTheEnd);
				*res = *_me.tensorObjectReadOnly;
				_out.reset(new internal::IndexedTensorMoveable<TensorNetwork>(res, _me.indices));
				TensorNetwork::add_network_to_network(std::move(*_out), std::move(_other));
				return true;
			} else {
				return false;
			}
		}
		return false;
	}
	
	template<bool isOperator>
	bool TTNetwork<isOperator>::specialized_sum_f(std::unique_ptr<internal::IndexedTensorMoveable<TensorNetwork>>& _out, internal::IndexedTensorReadOnly<TensorNetwork>&& _me, internal::IndexedTensorReadOnly<TensorNetwork>&& _other) {
		REQUIRE(_me.degree() == _other.degree(), "");
		
		_me.assign_indices();
		_other.assign_indices();
		
		// If the other is not a TT tensor (or stack) fall back to default summation (ie return false)
		const TTNetwork* otherTT = dynamic_cast<const TTNetwork*>( _other.tensorObjectReadOnly);
		const internal::TTStack<isOperator>* otherTTStack = dynamic_cast<const internal::TTStack<isOperator>*>( _other.tensorObjectReadOnly);
		if (!otherTT && !otherTTStack) { return false; }
		
		bool transposeRHS = false;
		if (!isOperator && _me.indices != _other.indices) {
			return false; //TODO we could do inverse order...
		} else if (isOperator) {
			// find index mid-points to compare the halves separately
			auto midIndexItr = _me.indices.begin();
			size_t spanSum = 0;
			while (spanSum < _me.degree() / 2) {
				REQUIRE(midIndexItr != _me.indices.end(), "Internal Error.");
				spanSum += midIndexItr->span;
				++midIndexItr;
			}
			auto otherMidIndexItr = _other.indices.begin();
			spanSum = 0;
			while (spanSum < _other.degree() / 2) {
				REQUIRE(otherMidIndexItr != _other.indices.end(), "Internal Error.");
				spanSum += otherMidIndexItr->span;
				++otherMidIndexItr;
			}
			
			if (_me.indices == _other.indices) { 
				REQUIRE(_me.tensorObjectReadOnly->dimensions == _other.tensorObjectReadOnly->dimensions, "TT sum requires both operants to share the same dimensions");
			} else {
				if (   !std::equal(_me.indices.begin(), midIndexItr, otherMidIndexItr) 
					|| !std::equal(midIndexItr, _me.indices.end(), _other.indices.begin())) 
				{
					return false;
				}
				// other is transposed compared to me
				for (size_t d=0; d<_me.degree(); ++d) {
					REQUIRE(_me.tensorObjectReadOnly->dimensions[d] == _other.tensorObjectReadOnly->dimensions[(d+_me.degree()/2)%_me.degree()], "sum requires identical dimensions");
				}
				transposeRHS = true;
			}
		}
		
		// TODO the order is not canonical, because if I am no Stack I don't have to know whether or not i am moveable
		// If I am in fact a TTTensorStack, we have to evaluate me to TTNetwork
		std::unique_ptr<internal::IndexedTensor<TensorNetwork>> meStorage;
		internal::IndexedTensorReadOnly<TensorNetwork> *realMePtr = &_me;
		internal::IndexedTensorMoveable<TensorNetwork> *movMe = dynamic_cast<internal::IndexedTensorMoveable<TensorNetwork> *>(&_me);
		if (movMe) {
			internal::TTStack<isOperator> *stackMe = dynamic_cast<internal::TTStack<isOperator> *>(movMe->tensorObject);
			if (stackMe) {
				meStorage.reset(new internal::IndexedTensor<TensorNetwork>(new TTNetwork(_me.degree()), _me.indices, true));
				std::move(*meStorage) = std::move(_me);
				realMePtr = meStorage.get();
			}
		} else {
			REQUIRE(!dynamic_cast<const internal::TTStack<isOperator> *>(_me.tensorObjectReadOnly),"ie - non-moveable TTStack detected");
		}
		internal::IndexedTensorReadOnly<TensorNetwork> &realMe = *realMePtr;
		
		// If other is in fact a TTTensorStack, we have to evaluate it to tttensor
		std::unique_ptr<TTNetwork> otherStorage;
		const TensorNetwork *realOtherPtr = _other.tensorObjectReadOnly;
		internal::IndexedTensorMoveable<TensorNetwork> *movOther = dynamic_cast<internal::IndexedTensorMoveable<TensorNetwork> *>(&_other);
		if (movOther) {
			internal::TTStack<isOperator> *stackOther = dynamic_cast<internal::TTStack<isOperator> *>(movOther->tensorObject);
			if (stackOther) {
				otherStorage.reset(new TTNetwork());
				(*otherStorage)(_other.indices) = std::move(_other);
				if (transposeRHS) {
					//NOTE will only be called in the operator case and is thus a nop
					reinterpret_cast<TTNetwork<true> *>(otherStorage.get())->transpose();
					transposeRHS = false;
				}
				realOtherPtr = otherStorage.get();
			}
		} else {
			REQUIRE(!dynamic_cast<const internal::TTStack<isOperator> *>(_other.tensorObjectReadOnly),"ie - non-moveable TTStack detected");
		}
		if (transposeRHS) {
			otherStorage.reset(new TTNetwork());
			(*otherStorage)(_other.indices) = std::move(_other);
			//NOTE will only be called in the operator case and is thus a nop
			reinterpret_cast<TTNetwork<true> *>(otherStorage.get())->transpose();
			transposeRHS = false;
			realOtherPtr = otherStorage.get();
		}
		const TensorNetwork &realOther = *realOtherPtr;
		
		// Number of components to create
		const size_t numComponents = realMe.degree()/N;
		
		_out.reset( new internal::IndexedTensorMoveable<TensorNetwork>( new TTNetwork(realMe.degree()), _me.indices));
		
		//The external dimensions are the same as the ones of the input
		_out->tensorObject->dimensions = realMe.tensorObjectReadOnly->dimensions;
		REQUIRE(realOther.dimensions == realMe.tensorObjectReadOnly->dimensions, "Internal Error");
		
		TTNetwork& outTensor = *static_cast<TTNetwork*>(_out->tensorObject);
		
		if (numComponents == 0) {
			(*outTensor.nodes[0].tensorObject)[0] = (*_me.tensorObjectReadOnly->nodes[0].tensorObject)[0] +  (*_other.tensorObjectReadOnly->nodes[0].tensorObject)[0];
			return true;
		}
		
		if(numComponents == 1) {
			// Create the one Node
			std::unique_ptr<Tensor> nextTensor;
			const Tensor &myComponent = *realMe.tensorObjectReadOnly->nodes[1].tensorObject.get();
			const Tensor &otherComponent = *realOther.nodes[1].tensorObject.get();
			if(myComponent.is_sparse() && otherComponent.is_sparse()) { // Both Sparse
				nextTensor.reset( new Tensor(myComponent));
				*static_cast<Tensor*>(nextTensor.get()) += (*static_cast<const Tensor*>(&otherComponent));
			} else { // at most one sparse
				if(myComponent.is_sparse()){
					nextTensor.reset(new Tensor(*static_cast<const Tensor*>(&myComponent)));
				} else {
					nextTensor.reset(new Tensor(*static_cast<const Tensor*>(&myComponent)));
				}
				if(otherComponent.is_sparse()){
					*static_cast<Tensor*>(nextTensor.get()) += static_cast<const Tensor&>(otherComponent);
				} else {
					*static_cast<Tensor*>(nextTensor.get()) += static_cast<const Tensor&>(otherComponent);
				}
			}
			
			outTensor.set_component(0, std::move(*nextTensor));
			return true;
		}
		
		const TTNetwork * const ttMe = static_cast<const TTNetwork*>(realMe.tensorObjectReadOnly);
		const TTNetwork * const ttOther = static_cast<const TTNetwork*>(realOtherPtr);
		
		PA_START;
		for(size_t position = 0; position < numComponents; ++position) {
			// Get current input nodes
			// TODO sparse
			REQUIRE(!realMe.tensorObjectReadOnly->nodes[position+1].tensorObject->is_sparse(), "sparse tensors in TT not supported (yet)");
			REQUIRE(!realOther.nodes[position+1].tensorObject->is_sparse(), "sparse tensors in TT not supported (yet)");
			Tensor &myComponent = *static_cast<Tensor*>(realMe.tensorObjectReadOnly->nodes[position+1].tensorObject.get());
			Tensor &otherComponent = *static_cast<Tensor*>(realOther.nodes[position+1].tensorObject.get());
			
			// Structure has to be (for degree 4)
			// (L1 R1) * ( L2 0  ) * ( L3 0  ) * ( L4 )
			// 			 ( 0  R2 )   ( 0  R3 )   ( R4 )
			
			// Create a Tensor for Node
			std::vector<size_t> nxtDimensions;
			if (position == 0) { 
				nxtDimensions.emplace_back(1);
			} else {
				nxtDimensions.emplace_back(myComponent.dimensions.front()+otherComponent.dimensions.front());
			}
			nxtDimensions.emplace_back(outTensor.dimensions[position]);
			if (isOperator) { nxtDimensions.emplace_back(outTensor.dimensions[position+numComponents]); }
			if (position == numComponents-1) {
				nxtDimensions.emplace_back(1);
			} else {
				nxtDimensions.emplace_back(myComponent.dimensions.back()+otherComponent.dimensions.back());
			}
			
			Tensor::Representation newRep = myComponent.is_sparse() || otherComponent.is_sparse() ? Tensor::Representation::Sparse : Tensor::Representation::Dense;
			REQUIRE(newRep == Tensor::Representation::Dense, "Sparse TT sum not yet implemented!");
			std::unique_ptr<Tensor> newComponent(new Tensor(std::move(nxtDimensions), newRep) );
			value_t * const componentData = static_cast<Tensor*>(newComponent.get())->get_unsanitized_dense_data();
			
			
			const size_t leftIdxOffset = newComponent->size/newComponent->dimensions.front();
			const size_t extIdxOffset = newComponent->dimensions.back();
			const size_t myLeftIdxOffset = myComponent.size/myComponent.dimensions.front();
			const size_t myExtIdxOffset = myComponent.dimensions.back();
			const size_t otherLeftIdxOffset = otherComponent.size/otherComponent.dimensions.front();
			const size_t otherExtIdxOffset = otherComponent.dimensions.back();
			const size_t otherGeneralOffset = (position == 0 ? 0 : myComponent.dimensions.front()*leftIdxOffset) + (position == numComponents-1 ? 0 : myComponent.dimensions.back());
			const size_t extDimSize = myComponent.dimensions[1] * (isOperator? myComponent.dimensions[2] : 1);
			
			// Copy own Tensor into place
			if (!ttMe->cannonicalized || position == ttMe->corePosition) {
				for(size_t leftIdx = 0; leftIdx < myComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied in one piece
						misc::copy_scaled(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset, 
												myComponent.factor, 
												myComponent.get_unsanitized_dense_data() + leftIdx*myLeftIdxOffset + extIdx*myExtIdxOffset, 
												myComponent.dimensions.back());
					}
				}
			} else {
				REQUIRE(!myComponent.has_factor(), "Only Core node is allowed to have a factor");
				for(size_t leftIdx = 0; leftIdx < myComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied as one piece
						misc::copy(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset, 
										myComponent.get_unsanitized_dense_data() + leftIdx*myLeftIdxOffset + extIdx*myExtIdxOffset, 
										myComponent.dimensions.back());
					}
				}
			}
			
			
			// Copy other Tensor into place
			if(!ttOther->cannonicalized || position == ttOther->corePosition) {
				for(size_t leftIdx = 0; leftIdx < otherComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied as one piece
						misc::copy_scaled(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset + otherGeneralOffset, 
												otherComponent.factor, 
												otherComponent.get_unsanitized_dense_data() + leftIdx*otherLeftIdxOffset + extIdx*otherExtIdxOffset, 
												otherComponent.dimensions.back());
					}
				}
			} else {
				REQUIRE(!otherComponent.has_factor(), "Only Core node is allowed to have a factor");
				for(size_t leftIdx = 0; leftIdx < otherComponent.dimensions.front(); ++leftIdx) {
					for(size_t extIdx = 0; extIdx < extDimSize; ++extIdx) {
						// RightIdx can be copied as one piece
						misc::copy(componentData + leftIdx*leftIdxOffset + extIdx*extIdxOffset + otherGeneralOffset, 
										otherComponent.get_unsanitized_dense_data() + leftIdx*otherLeftIdxOffset + extIdx*otherExtIdxOffset, 
										otherComponent.dimensions.back());
					}
				}
			}
			
			outTensor.set_component(position, std::move(*newComponent));
		}
		
		PA_END("ADD/SUB", "TTNetwork ADD/SUB", std::string("Dims:")+misc::to_string(outTensor.dimensions)+" Ranks: "+misc::to_string(outTensor.ranks()));
		
		// TODO profiler should warn if other->corePos differs
		
		if (ttMe->cannonicalized) {
			REQUIRE(!outTensor.cannonicalized, "Internal Error.");
			outTensor.move_core(ttMe->corePosition);
			REQUIRE(!outTensor.exceeds_maximal_ranks(), outTensor.dimensions << " rank: " << outTensor.ranks());
		}
		
		return true;
	}
	
	
	template<bool isOperator>
	void TTNetwork<isOperator>::specialized_evaluation(internal::IndexedTensorWritable<TensorNetwork>&& _me, internal::IndexedTensorReadOnly<TensorNetwork>&& _other) {
		REQUIRE(_me.tensorObject == this, "Internal Error.");
		
		_me.assign_indices(_other.degree());
		_other.assign_indices();
		const size_t numComponents = _other.degree()/N;
		TTNetwork& meTTN = static_cast<TTNetwork&>(*_me.tensorObject);
		
		// First check whether the other is a TTNetwork as well, otherwise we can skip to fallback
		const TTNetwork* const otherTTN = dynamic_cast<const TTNetwork*>(_other.tensorObjectReadOnly);
		const internal::TTStack<isOperator>* const otherTTStack = dynamic_cast<const internal::TTStack<isOperator>*>(_other.tensorObjectReadOnly);
		internal::IndexedTensorMoveable<TensorNetwork> *movOther = dynamic_cast<internal::IndexedTensorMoveable<TensorNetwork> *>(&_other);
		
		if(otherTTN || otherTTStack) {
			if (otherTTStack) {
				REQUIRE(movOther, "Not moveable TTStack encountered...");
				internal::TTStack<isOperator>::contract_stack(std::move(*movOther));
			}
			
			// Check whether the index order coincides
			if (_me.indices == _other.indices) {
				if (otherTTN) {
					meTTN = *otherTTN;
				} else {
					_me.tensorObject->operator=(*_other.tensorObjectReadOnly);
					meTTN.cannonicalized = false;
					if (otherTTStack->cannonicalization_required) {
						meTTN.move_core(otherTTStack->futureCorePosition);
					}
				}
				return;
			}
			
			// For TTOperators also check whether the index order is transposed
			if (isOperator) {
				bool transposed = false;
				
				auto midIndexItr = _me.indices.begin();
				size_t spanSum = 0;
				while (spanSum < numComponents) {
					REQUIRE(midIndexItr != _me.indices.end(), "Internal Error.");
					spanSum += midIndexItr->span;
					++midIndexItr;
				}
				if (spanSum == numComponents) {
					// Transposition possible on my end
					auto otherMidIndexItr = _other.indices.begin();
					spanSum = 0;
					while (spanSum < numComponents) {
						REQUIRE(otherMidIndexItr != _other.indices.end(), "Internal Error.");
						spanSum += otherMidIndexItr->span;
						++otherMidIndexItr;
					}
					if (spanSum == numComponents) {
						// Other tensor also transposable
						transposed = (std::equal(_me.indices.begin(), midIndexItr, otherMidIndexItr)) 
						&& (std::equal(midIndexItr, _me.indices.end(), _other.indices.begin()));
					}
				}
				
				if (transposed) {
					if (otherTTN) {
						meTTN = *otherTTN;
					} else {
						_me.tensorObject->operator=(*_other.tensorObjectReadOnly);
						meTTN.cannonicalized = false;
						if (otherTTStack->cannonicalization_required) {
							meTTN.move_core(otherTTStack->futureCorePosition);
						}
					}
					require_correct_format();
					dynamic_cast<TTOperator*>(_me.tensorObject)->transpose(); // NOTE will never be called if !isOperator
					return;
				}
			}
		}
		
		// Use Tensor fallback
		CHECK(_other.tensorObjectReadOnly->nodes.size() <= 1, warning, "Assigning a general tensor network to TTOperator not yet implemented. casting to fullTensor first");
		Tensor otherFull(*_other.tensorObjectReadOnly);
		Tensor otherReordered;
		otherReordered(_me.indices) = otherFull(_other.indices);
		
		// Cast to TTNetwork
		*_me.tensorObject = TTNetwork(std::move(otherReordered));
	}
	
	
	// Explicit instantiation of the two template parameters that will be implemented in the xerus library
	template class TTNetwork<false>;
	template class TTNetwork<true>;
	
	
	
	template<bool isOperator>
	TTNetwork<isOperator> operator+(TTNetwork<isOperator> _lhs, const TTNetwork<isOperator>& _rhs) {
		_lhs += _rhs;
		return _lhs;
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator> operator-(TTNetwork<isOperator> _lhs, const TTNetwork<isOperator>& _rhs) {
		_lhs -= _rhs;
		return _lhs;
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator> operator*(TTNetwork<isOperator> _network, const value_t _factor) {
		_network *= _factor;
		return _network;
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator> operator*(const value_t _factor, TTNetwork<isOperator> _network) {
		_network *= _factor;
		return _network;
	}
	
	
	template<bool isOperator>
	TTNetwork<isOperator> operator/(TTNetwork<isOperator> _network, const value_t _divisor) {
		_network /= _divisor;
		return _network;
	}
	
	//Explicit instantiation for both types
	template TTNetwork<false> operator+(TTNetwork<false> _lhs, const TTNetwork<false>& _rhs);
	template TTNetwork<true> operator+(TTNetwork<true> _lhs, const TTNetwork<true>& _rhs);
	template TTNetwork<false> operator-(TTNetwork<false> _lhs, const TTNetwork<false>& _rhs);
	template TTNetwork<true> operator-(TTNetwork<true> _lhs, const TTNetwork<true>& _rhs);
	template TTNetwork<false> operator*(TTNetwork<false> _network, const value_t _factor);
	template TTNetwork<true> operator*(TTNetwork<true> _network, const value_t _factor);
	template TTNetwork<false> operator*(const value_t _factor, TTNetwork<false> _network);
	template TTNetwork<true> operator*(const value_t _factor, TTNetwork<true> _network);
	template TTNetwork<false> operator/(TTNetwork<false> _network, const value_t _divisor);
	template TTNetwork<true> operator/(TTNetwork<true> _network, const value_t _divisor);
}
