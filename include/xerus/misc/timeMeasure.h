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

#pragma once

#include "standard.h"

namespace xerus {
    namespace misc {

        ///@brief: Returns the time since epoche in microseconds.
        size_t uTime();

        ///@brief: Returns the time since epoche in milliseconds.
        size_t mTime();

        class TimeMeasure
        {
            public:
                /// Creation time.
                const size_t timeStart;
                
                /// Time of last step.
                size_t timeStep;
                
                /// Constructor.
                TimeMeasure();
                
                /// Sets new step & returns time since last Step in microseconds.
                size_t step();
                
                /// Returns time since last step  in microseconds.
                size_t get() const;
                
                /// Returns the total time since creation of the TimeMeasure  in microseconds.
                size_t getTotal() const;
        }; 

    }
}
