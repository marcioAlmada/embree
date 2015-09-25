// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "intersector_epilog.h"

/*! This intersector implements a modified version of the Moeller
 *  Trumbore intersector from the paper "Fast, Minimum Storage
 *  Ray-Triangle Intersection". In contrast to the paper we
 *  precalculate some factors and factor the calculations differently
 *  to allow precalculating the cross product e1 x e2. The resulting
 *  algorithm is similar to the fastest one of the paper "Optimizing
 *  Ray-Triangle Intersection via Automated Search". */

namespace embree
{
  namespace isa
  {
    template<int M, typename Epilog>
    __forceinline bool moeller_trumbore_intersect1(Ray& ray, 
                                                   const Vec3<vfloat<M>>& tri_v0, 
                                                   const Vec3<vfloat<M>>& tri_e1, 
                                                   const Vec3<vfloat<M>>& tri_e2, 
                                                   const Vec3<vfloat<M>>& tri_Ng,
                                                   const Epilog& epilog)
    {
      /* calculate denominator */
      typedef Vec3<vfloat<M>> Vec3vfM;
      const Vec3vfM O = Vec3vfM(ray.org);
      const Vec3vfM D = Vec3vfM(ray.dir);
      const Vec3vfM C = Vec3vfM(tri_v0) - O;
      const Vec3vfM R = cross(D,C);
      const vfloat<M> den = dot(Vec3vfM(tri_Ng),D);
      const vfloat<M> absDen = abs(den);
      const vfloat<M> sgnDen = signmsk(den);
      
      /* perform edge tests */
      const vfloat<M> U = dot(R,Vec3vfM(tri_e2)) ^ sgnDen;
      const vfloat<M> V = dot(R,Vec3vfM(tri_e1)) ^ sgnDen;
      
      /* perform backface culling */
#if defined(RTCORE_BACKFACE_CULLING)
      vbool<M> valid = (den > vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#else
      vbool<M> valid = (den != vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#endif
      if (likely(none(valid))) return false;
      
      /* perform depth test */
      const vfloat<M> T = dot(Vec3vfM(tri_Ng),C) ^ sgnDen;
      valid &= (T > absDen*vfloat<M>(ray.tnear)) & (T < absDen*vfloat<M>(ray.tfar));
      if (likely(none(valid))) return false;

      /* update hit information */
      return epilog(valid,[&] () {
          const vfloat<M> rcpAbsDen = rcp(absDen);
          const vfloat<M> u = U * rcpAbsDen;
          const vfloat<M> v = V * rcpAbsDen;
          const vfloat<M> t = T * rcpAbsDen;
          return std::make_tuple(u,v,t,tri_Ng);
        });
    }

    template<int M, typename Epilog>
    __forceinline bool moeller_trumbore_intersect1(Ray& ray, 
                                                   const Vec3<vfloat<M>>& v0, 
                                                   const Vec3<vfloat<M>>& v1, 
                                                   const Vec3<vfloat<M>>& v2, 
                                                   const Epilog& epilog)
    {
      const Vec3<vfloat<M>> e1 = v0-v1;
      const Vec3<vfloat<M>> e2 = v2-v0;
      const Vec3<vfloat<M>> Ng = cross(e1,e2);
      return moeller_trumbore_intersect1<M>(ray,v0,e1,e2,Ng,epilog);
    }
    

///////////////////////////////////////////////////////////////////////
    
    /*! Intersects K rays with one of M triangles. */
    template<int K, int M, typename Epilog>
      __forceinline vbool<K> moeller_trumbore_intersectK(const vbool<K>& valid0, 
                                                         RayK<K>& ray, 
                                                         const Vec3<vfloat<K>>& tri_v0, 
                                                         const Vec3<vfloat<K>>& tri_e1, 
                                                         const Vec3<vfloat<K>>& tri_e2, 
                                                         const Vec3<vfloat<K>>& tri_Ng, 
                                                         const Epilog& epilog)
    {
      /* ray SIMD type shortcuts */
      typedef Vec3<vfloat<K>> rsimd3f;
      
      /* calculate denominator */
      vbool<K> valid = valid0;
      const rsimd3f C = tri_v0 - ray.org;
      const rsimd3f R = cross(ray.dir,C);
      const vfloat<K> den = dot(tri_Ng,ray.dir);
      const vfloat<K> absDen = abs(den);
      const vfloat<K> sgnDen = signmsk(den);
      
      /* test against edge p2 p0 */
      const vfloat<K> U = dot(R,tri_e2) ^ sgnDen;
      valid &= U >= 0.0f;
      if (likely(none(valid))) return false;
      
      /* test against edge p0 p1 */
      const vfloat<K> V = dot(R,tri_e1) ^ sgnDen;
      valid &= V >= 0.0f;
      if (likely(none(valid))) return false;
      
      /* test against edge p1 p2 */
      const vfloat<K> W = absDen-U-V;
      valid &= W >= 0.0f;
      if (likely(none(valid))) return false;
      
      /* perform depth test */
      const vfloat<K> T = dot(tri_Ng,C) ^ sgnDen;
      valid &= (T >= absDen*ray.tnear) & (absDen*ray.tfar >= T);
      if (unlikely(none(valid))) return false;
      
      /* perform backface culling */
#if defined(RTCORE_BACKFACE_CULLING)
      valid &= den > vfloat<K>(zero);
      if (unlikely(none(valid))) return false;
#else
      valid &= den != vfloat<K>(zero);
      if (unlikely(none(valid))) return false;
#endif
      
      /* calculate hit information */
      return epilog(valid,[&] () {
          const vfloat<K> rcpAbsDen = rcp(absDen);
          const vfloat<K> u = U*rcpAbsDen;
          const vfloat<K> v = V*rcpAbsDen;
          const vfloat<K> t = T*rcpAbsDen;
          return std::make_tuple(u,v,t,tri_Ng);
        });
    }

    /*! Intersects K rays with one of M triangles. */
    template<int K, int M, typename Epilog>
      __forceinline vbool<K> moeller_trumbore_intersectK(const vbool<K>& valid0, 
                                                         RayK<K>& ray, 
                                                         const Vec3<vfloat<K>>& tri_v0, 
                                                         const Vec3<vfloat<K>>& tri_v1, 
                                                         const Vec3<vfloat<K>>& tri_v2, 
                                                         const Epilog& epilog)
    {
      typedef Vec3<vfloat<K>> tsimd3f;
      const tsimd3f e1 = tri_v0-tri_v1;
      const tsimd3f e2 = tri_v2-tri_v0;
      const tsimd3f Ng = cross(e1,e2);
      return moeller_trumbore_intersectK<K,M>(valid0,ray,tri_v0,e1,e2,Ng,epilog);
    }
    

    
    

//////////////////////////////////////////////////////////////////

    
    /*! Intersect a ray with the 4 triangles and updates the hit. */
    template<bool enableIntersectionFilter, typename tsimdf, typename tsimdi, typename RayK>
      __forceinline void triangle_intersect_moeller_trumbore_k(RayK& ray, size_t k,
                                                             const Vec3<tsimdf>& tri_v0, 
							     const Vec3<tsimdf>& tri_e1, 
							     const Vec3<tsimdf>& tri_e2, 
							     const Vec3<tsimdf>& tri_Ng, 
                                                             const tsimdi& tri_geomIDs, 
							     const tsimdi& tri_primIDs, 
							     Scene* scene)
    {
      /* type shortcuts */
      typedef typename RayK::simdf rsimdf;
      typedef typename tsimdf::Bool tsimdb;
      typedef Vec3<tsimdf> tsimd3f;
      
      /* calculate denominator */
      const tsimd3f O = broadcast<tsimdf>(ray.org,k);
      const tsimd3f D = broadcast<tsimdf>(ray.dir,k);
      const tsimd3f C = tsimd3f(tri_v0) - O;
      const tsimd3f R = cross(D,C);
      const tsimdf den = dot(tsimd3f(tri_Ng),D);
      const tsimdf absDen = abs(den);
      const tsimdf sgnDen = signmsk(den);
      
      /* perform edge tests */
      const tsimdf U = dot(R,tsimd3f(tri_e2)) ^ sgnDen;
      const tsimdf V = dot(R,tsimd3f(tri_e1)) ^ sgnDen;
      
      /* perform backface culling */
#if defined(RTCORE_BACKFACE_CULLING)
      tsimdb valid = (den > tsimdf(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#else
      tsimdb valid = (den != tsimdf(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#endif
      if (likely(none(valid))) return;
      
      /* perform depth test */
      const tsimdf T = dot(tsimd3f(tri_Ng),C) ^ sgnDen;
      valid &= (T > absDen*tsimdf(ray.tnear[k])) & (T < absDen*tsimdf(ray.tfar[k]));
      if (likely(none(valid))) return;
      
      /* calculate hit information */
      const tsimdf rcpAbsDen = rcp(absDen);
      const tsimdf u = U * rcpAbsDen;
      const tsimdf v = V * rcpAbsDen;
      const tsimdf t = T * rcpAbsDen;
      size_t i = select_min(valid,t);
      int geomID = tri_geomIDs[i];
      
      /* intersection filter test */
#if defined(RTCORE_INTERSECTION_FILTER) || defined(RTCORE_RAY_MASK)
      goto entry;
      while (true) 
      {
        if (unlikely(none(valid))) return;
        i = select_min(valid,t);
        geomID = tri_geomIDs[i];
      entry:
        Geometry* geometry = scene->get(geomID);
        
#if defined(RTCORE_RAY_MASK)
        /* goto next hit if mask test fails */
        if ((geometry->mask & ray.mask[k]) == 0) {
          valid[i] = 0;
          continue;
        }
#endif
        
#if defined(RTCORE_INTERSECTION_FILTER) 
        /* call intersection filter function */
        if (enableIntersectionFilter) {
          if (unlikely(geometry->hasIntersectionFilter<rsimdf>())) {
            Vec3fa Ng = Vec3fa(tri_Ng.x[i],tri_Ng.y[i],tri_Ng.z[i]);
            if (runIntersectionFilter(geometry,ray,k,u[i],v[i],t[i],Ng,geomID,tri_primIDs[i])) return;
            valid[i] = 0;
            continue;
          }
        }
#endif
        break;
      }
#endif
      
      /* update hit information */
      ray.u[k] = u[i];
      ray.v[k] = v[i];
      ray.tfar[k] = t[i];
      ray.Ng.x[k] = tri_Ng.x[i];
      ray.Ng.y[k] = tri_Ng.y[i];
      ray.Ng.z[k] = tri_Ng.z[i];
      ray.geomID[k] = geomID;
      ray.primID[k] = tri_primIDs[i];
    }
    
    template<bool enableIntersectionFilter, typename tsimdf, typename tsimdi, typename RayK>
      __forceinline void triangle_intersect_moeller_trumbore_k(RayK& ray, size_t k,
                                                             const Vec3<tsimdf>& v0, const Vec3<tsimdf>& v1, const Vec3<tsimdf>& v2,
                                                             const tsimdi& tri_geomIDs, const tsimdi& tri_primIDs, const size_t i, Scene* scene)
    {
      typedef Vec3<tsimdf> tsimd3f;
      const tsimd3f e1 = v0-v1;
      const tsimd3f e2 = v2-v0;
      const tsimd3f Ng = cross(e1,e2);
      triangle_intersect_moeller_trumbore_k<enableIntersectionFilter>(ray,k,v0,e1,e2,Ng,tri_geomIDs,tri_primIDs,i,scene);
    }
    
    
    /*! Test if the ray is occluded by one of the triangles. */
    template<bool enableIntersectionFilter, typename tsimdf, typename tsimdi, typename RayK>
      __forceinline bool triangle_occluded_moeller_trumbore_k(RayK& ray, size_t k, 
                                                            const Vec3<tsimdf>& tri_v0, const Vec3<tsimdf>& tri_e1, const Vec3<tsimdf>& tri_e2, const Vec3<tsimdf>& tri_Ng, 
                                                            const tsimdi& tri_geomIDs, const tsimdi& tri_primIDs, Scene* scene)
    {
      /* type shortcuts */
      typedef typename RayK::simdf rsimdf;
      typedef typename tsimdf::Bool tsimdb;
      typedef Vec3<tsimdf> tsimd3f;
      
      /* calculate denominator */
      const tsimd3f O = broadcast<tsimdf>(ray.org,k);
      const tsimd3f D = broadcast<tsimdf>(ray.dir,k);
      const tsimd3f C = tsimd3f(tri_v0) - O;
      const tsimd3f R = cross(D,C);
      const tsimdf den = dot(tsimd3f(tri_Ng),D);
      const tsimdf absDen = abs(den);
      const tsimdf sgnDen = signmsk(den);
      
      /* perform edge tests */
      const tsimdf U = dot(R,tsimd3f(tri_e2)) ^ sgnDen;
      const tsimdf V = dot(R,tsimd3f(tri_e1)) ^ sgnDen;
      const tsimdf W = absDen-U-V;
      tsimdb valid = (U >= 0.0f) & (V >= 0.0f) & (W >= 0.0f);
      if (unlikely(none(valid))) return false;
      
      /* perform depth test */
      const tsimdf T = dot(tsimd3f(tri_Ng),C) ^ sgnDen;
      valid &= (T >= absDen*tsimdf(ray.tnear[k])) & (absDen*tsimdf(ray.tfar[k]) >= T);
      if (unlikely(none(valid))) return false;
      
      /* perform backface culling */
#if defined(RTCORE_BACKFACE_CULLING)
      valid &= den > tsimdf(zero);
      if (unlikely(none(valid))) return false;
#else
      valid &= den != tsimdf(zero);
      if (unlikely(none(valid))) return false;
#endif
      
      /* intersection filter test */
#if defined(RTCORE_INTERSECTION_FILTER) || defined(RTCORE_RAY_MASK)
      size_t m=movemask(valid);
      goto entry;
      while (true)
      {  
        if (unlikely(m == 0)) return false;
      entry:
        size_t i=__bsf(m);
        const int geomID = tri_geomIDs[i];
        Geometry* geometry = scene->get(geomID);
        
#if defined(RTCORE_RAY_MASK)
        /* goto next hit if mask test fails */
        if ((geometry->mask & ray.mask[k]) == 0) {
          m=__btc(m,i);
          continue;
        }
#endif
        
#if defined(RTCORE_INTERSECTION_FILTER)
        /* execute occlusion filer */
        if (enableIntersectionFilter) {
          if (unlikely(geometry->hasOcclusionFilter<rsimdf>())) 
          {
            const tsimdf rcpAbsDen = rcp(absDen);
            const tsimdf u = U * rcpAbsDen;
            const tsimdf v = V * rcpAbsDen;
            const tsimdf t = T * rcpAbsDen;
            const Vec3fa Ng = Vec3fa(tri_Ng.x[i],tri_Ng.y[i],tri_Ng.z[i]);
            if (runOcclusionFilter(geometry,ray,k,u[i],v[i],t[i],Ng,geomID,tri_primIDs[i])) return true;
            m=__btc(m,i);
            continue;
          }
        }
#endif
        break;
      }
#endif
      
      return true;
    }
    
    template<bool enableIntersectionFilter, typename tsimdf, typename tsimdi, typename RayK>
      __forceinline bool triangle_occluded_moeller_trumbore_k(RayK& ray, size_t k,
                                                            const Vec3<tsimdf>& v0, const Vec3<tsimdf>& v1, const Vec3<tsimdf>& v2,
                                                            const tsimdi& tri_geomIDs, const tsimdi& tri_primIDs, const size_t i, Scene* scene)
    {
      typedef Vec3<tsimdf> tsimd3f;
      const tsimd3f e1 = v0-v1;
      const tsimd3f e2 = v2-v0;
      const tsimd3f Ng = cross(e1,e2);
      return triangle_occluded_moeller_trumbore_k<enableIntersectionFilter>(ray,k,v0,e1,e2,Ng,tri_geomIDs,tri_primIDs,i,scene);
    }
    
    /*! Intersects N triangles with 1 ray */
    template<typename TriangleN, bool enableIntersectionFilter>
      struct TriangleNIntersector1MoellerTrumbore
      {
        enum { M = TriangleN::M };
        typedef TriangleN Primitive;
        
        struct Precalculations {
          __forceinline Precalculations (const Ray& ray, const void* ptr) {}
        };
        
        /*! Intersect a ray with the N triangles and updates the hit. */
        static __forceinline void intersect(const Precalculations& pre, Ray& ray, const TriangleN& tri, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(normal.trav_prims,1,1,1);
          moeller_trumbore_intersect1<M>(ray,tri.v0,tri.e1,tri.e2,tri.Ng,
                                         Intersect1Epilog<M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,scene,geomID_to_instID));
        }
        
        /*! Test if the ray is occluded by one of N triangles. */
        static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const TriangleN& tri, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(shadow.trav_prims,1,1,1);
          return moeller_trumbore_intersect1<M>(ray,tri.v0,tri.e1,tri.e2,tri.Ng,
                                                Occluded1Epilog<M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,scene,geomID_to_instID));
        }
      };


    /*! Intersects N/2 triangle pairs with 1 ray */
    template<typename TrianglePairsN, bool enableIntersectionFilter>
      struct TrianglePairsNIntersector1MoellerTrumbore // FIXME: not working
      {
        enum { M = TrianglePairsN::M };
        typedef TrianglePairsN Primitive;
        
        struct Precalculations {
          __forceinline Precalculations (const Ray& ray, const void* ptr) {}
        };
        
        /*! Intersect a ray with the N/2 triangle pairs and updates the hit. */
        static __forceinline void intersect(const Precalculations& pre, Ray& ray, const TrianglePairsN& tri, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(normal.trav_prims,1,1,1);
          moeller_trumbore_intersect1<M>(ray,tri.v0,tri.e1,tri.e2,
                                         Intersect1Epilog<M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,scene,geomID_to_instID));
        }
        
        /*! Test if the ray is occluded by one of N/2 triangle pairs. */
        static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const TrianglePairsN& tri, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(shadow.trav_prims,1,1,1);
          return moeller_trumbore_intersect1<M>(ray,tri.v0,tri.e1,tri.e2,
                                                Occluded1Epilog<M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,scene,geomID_to_instID));
        }
      };
    
    /*! Intersector for M triangles with K rays. */
    template<typename RayK, typename TriangleM, bool enableIntersectionFilter>
      struct TriangleNIntersectorMMoellerTrumbore
      {
        enum { K = RayK::K };
        enum { M = TriangleM::M };
        typedef TriangleM Primitive;
        
        /* triangle SIMD type shortcuts */
        typedef typename TriangleM::simdb tsimdb;
        typedef typename TriangleM::simdf tsimdf;
        typedef Vec3<tsimdf> tsimd3f;
        
        /* ray SIMD type shortcuts */
        typedef typename RayK::simdb rsimdb;
        typedef typename RayK::simdf rsimdf;
        typedef typename RayK::simdi rsimdi;
        typedef Vec3<rsimdf> rsimd3f;
        
        struct Precalculations {
          __forceinline Precalculations (const rsimdb& valid, const RayK& ray) {}
        };
        
        /*! Intersects a M rays with N triangles. */
        static __forceinline void intersect(const rsimdb& valid_i, Precalculations& pre, RayK& ray, const TriangleM& tri, Scene* scene)
        {
          for (size_t i=0; i<TriangleM::max_size(); i++)
          {
            if (!tri.valid(i)) break;
            STAT3(normal.trav_prims,1,popcnt(valid_i),RayK::size());
            const rsimd3f p0 = broadcast<rsimdf>(tri.v0,i);
            const rsimd3f e1 = broadcast<rsimdf>(tri.e1,i);
            const rsimd3f e2 = broadcast<rsimdf>(tri.e2,i);
            const rsimd3f Ng = broadcast<rsimdf>(tri.Ng,i);
            moeller_trumbore_intersectK<K,M>(valid_i,ray,p0,e1,e2,Ng,
                                             IntersectKEpilog<K,M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,i,scene));
          }
        }
        
        /*! Test for M rays if they are occluded by any of the N triangle. */
        static __forceinline rsimdb occluded(const rsimdb& valid_i, Precalculations& pre, RayK& ray, const TriangleM& tri, Scene* scene)
        {
          rsimdb valid0 = valid_i;
          
          for (size_t i=0; i<TriangleM::max_size(); i++)
          {
            if (!tri.valid(i)) break;
            STAT3(shadow.trav_prims,1,popcnt(valid0),RayK::size());
            const rsimd3f p0 = broadcast<rsimdf>(tri.v0,i);
            const rsimd3f e1 = broadcast<rsimdf>(tri.e1,i);
            const rsimd3f e2 = broadcast<rsimdf>(tri.e2,i);
            const rsimd3f Ng = broadcast<rsimdf>(tri.Ng,i);
            moeller_trumbore_intersectK<K,M>(valid0,ray,p0,e1,e2,Ng,
                                             OccludedKEpilog<K,M,enableIntersectionFilter>(valid0,ray,tri.geomIDs,tri.primIDs,i,scene));
            if (none(valid0)) break;
          }
          return !valid0;
        }
        
        /*! Intersect a ray with the 4 triangles and updates the hit. */
        static __forceinline void intersect(Precalculations& pre, RayK& ray, size_t k, const TriangleM& tri, Scene* scene)
        {
          STAT3(normal.trav_prims,1,1,1);
          triangle_intersect_moeller_trumbore_k<enableIntersectionFilter>(ray,k,tri.v0,tri.e1,tri.e2,tri.Ng,tri.geomIDs,tri.primIDs,scene);
        }
        
        /*! Test if the ray is occluded by one of the triangles. */
        static __forceinline bool occluded(Precalculations& pre, RayK& ray, size_t k, const TriangleM& tri, Scene* scene)
        {
          STAT3(shadow.trav_prims,1,1,1);
          return triangle_occluded_moeller_trumbore_k<enableIntersectionFilter>(ray,k,tri.v0,tri.e1,tri.e2,tri.Ng,tri.geomIDs,tri.primIDs,scene);
        }
      };
    
    /*! Intersects N triangles with 1 ray */
    template<typename TriangleNMblur, bool enableIntersectionFilter>
      struct TriangleNMblurIntersector1MoellerTrumbore
      {
        enum { M = TriangleNMblur::M };
        typedef TriangleNMblur Primitive;
        
        struct Precalculations {
          __forceinline Precalculations (const Ray& ray, const void* ptr) {}
        };
        
        /*! Intersect a ray with the N triangles and updates the hit. */
        static __forceinline void intersect(const Precalculations& pre, Ray& ray, const TriangleNMblur& tri, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(normal.trav_prims,1,1,1);
          const vfloat<M> time = ray.time;
          const Vec3<vfloat<M>> v0 = tri.v0 + time*tri.dv0;
          const Vec3<vfloat<M>> v1 = tri.v1 + time*tri.dv1;
          const Vec3<vfloat<M>> v2 = tri.v2 + time*tri.dv2;
          moeller_trumbore_intersect1<M>(ray,v0,v1,v2,
                                         Intersect1Epilog<M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,scene,geomID_to_instID));
        }
        
        /*! Test if the ray is occluded by one of N triangles. */
        static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const TriangleNMblur& tri, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(shadow.trav_prims,1,1,1);
          const vfloat<M> time = ray.time;
          const Vec3<vfloat<M>> v0 = tri.v0 + time*tri.dv0;
          const Vec3<vfloat<M>> v1 = tri.v1 + time*tri.dv1;
          const Vec3<vfloat<M>> v2 = tri.v2 + time*tri.dv2;
          return moeller_trumbore_intersect1<M>(ray,v0,v1,v2,
                                                Occluded1Epilog<M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,scene,geomID_to_instID));
        }
      };
    
    /*! Intersector for M triangles with K rays. */
    template<typename RayK, typename TriangleMMblur, bool enableIntersectionFilter>
      struct TriangleNMblurIntersectorMMoellerTrumbore
      {
        enum { K = RayK::K };
        enum { M = TriangleMMblur::M };
        typedef TriangleMMblur Primitive;
        
        /* triangle SIMD type shortcuts */
        typedef typename TriangleMMblur::simdb tsimdb;
        typedef typename TriangleMMblur::simdf tsimdf;
        typedef Vec3<tsimdf> tsimd3f;
        
        /* ray SIMD type shortcuts */
        typedef typename RayK::simdb rsimdb;
        typedef typename RayK::simdf rsimdf;
        typedef typename RayK::simdi rsimdi;
        typedef Vec3<rsimdf> rsimd3f;
        
        struct Precalculations {
          __forceinline Precalculations (const rsimdb& valid, const RayK& ray) {}
        };
        
        /*! Intersects a M rays with N triangles. */
        static __forceinline void intersect(const rsimdb& valid_i, Precalculations& pre, RayK& ray, const TriangleMMblur& tri, Scene* scene)
        {
          for (size_t i=0; i<TriangleMMblur::max_size(); i++)
          {
            if (!tri.valid(i)) break;
            STAT3(normal.trav_prims,1,popcnt(valid_i),RayK::size());
            const rsimdf time = ray.time;
            const rsimd3f v0 = broadcast<rsimdf>(tri.v0,i) + time*broadcast<rsimdf>(tri.dv0,i);
            const rsimd3f v1 = broadcast<rsimdf>(tri.v1,i) + time*broadcast<rsimdf>(tri.dv1,i);
            const rsimd3f v2 = broadcast<rsimdf>(tri.v2,i) + time*broadcast<rsimdf>(tri.dv2,i);
            moeller_trumbore_intersectK<K,M>(valid_i,ray,v0,v1,v2,
                                             IntersectKEpilog<K,M,enableIntersectionFilter>(ray,tri.geomIDs,tri.primIDs,i,scene));
          }
        }
        
        /*! Test for M rays if they are occluded by any of the N triangle. */
        static __forceinline rsimdb occluded(const rsimdb& valid_i, Precalculations& pre, RayK& ray, const TriangleMMblur& tri, Scene* scene)
        {
          rsimdb valid0 = valid_i;
          
          for (size_t i=0; i<TriangleMMblur::max_size(); i++)
          {
            if (!tri.valid(i)) break;
            STAT3(shadow.trav_prims,1,popcnt(valid0),RayK::size());
            const rsimdf time = ray.time;
            const rsimd3f v0 = broadcast<rsimdf>(tri.v0,i) + time*broadcast<rsimdf>(tri.dv0,i);
            const rsimd3f v1 = broadcast<rsimdf>(tri.v1,i) + time*broadcast<rsimdf>(tri.dv1,i);
            const rsimd3f v2 = broadcast<rsimdf>(tri.v2,i) + time*broadcast<rsimdf>(tri.dv2,i);
            moeller_trumbore_intersectK<K,M>(valid0,ray,v0,v1,v2,
                                             OccludedKEpilog<K,M,enableIntersectionFilter>(valid0,ray,tri.geomIDs,tri.primIDs,i,scene));
            if (none(valid0)) break;
          }
          return !valid0;
        }
        
        /*! Intersect a ray with the N triangles and updates the hit. */
        static __forceinline void intersect(Precalculations& pre, RayK& ray, size_t k, const TriangleMMblur& tri, Scene* scene)
        {
          STAT3(normal.trav_prims,1,1,1);
          const tsimdf time = broadcast<tsimdf>(ray.time,k);
          const tsimd3f v0 = tri.v0 + time*tri.dv0;
          const tsimd3f v1 = tri.v1 + time*tri.dv1;
          const tsimd3f v2 = tri.v2 + time*tri.dv2;
          triangle_intersect_moeller_trumbore_k<enableIntersectionFilter>(ray,k,v0,v1,v2,tri.geomIDs,tri.primIDs,scene);
        }
        
        /*! Test if the ray is occluded by one of the N triangles. */
        static __forceinline bool occluded(Precalculations& pre, RayK& ray, size_t k, const TriangleMMblur& tri, Scene* scene)
        {
          STAT3(shadow.trav_prims,1,1,1);
          const tsimdf time = broadcast<tsimdf>(ray.time,k);
          const tsimd3f v0 = tri.v0 + time*tri.dv0;
          const tsimd3f v1 = tri.v1 + time*tri.dv1;
          const tsimd3f v2 = tri.v2 + time*tri.dv2;
          return triangle_occluded_moeller_trumbore_k<enableIntersectionFilter>(ray,k,v0,v1,v2,tri.geomIDs,tri.primIDs,scene);
        }
      };
  }
}
