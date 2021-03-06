#pragma once

// ${generated_comment}

#include "ATen/Type.h"
#include "ATen/Context.h"
#include "ATen/TensorMethods.h"
#include "ATen/CheckGenerator.h"

#ifdef _MSC_VER
#ifdef Type
#undef Type
#endif
#endif

namespace at {

struct ${Type} final : public Type {
  explicit ${Type}();
  virtual ScalarType scalarType() const override;
  virtual Backend backend() const override;
  virtual bool is_cuda() const override;
  virtual bool is_sparse() const override;
  virtual bool is_distributed() const override;
  virtual Storage storage(bool resizable = false) const override;
  virtual Storage storage(size_t size, bool resizable = false) const override;
  virtual Storage storageFromBlob(void * data, int64_t size, const std::function<void(void*)> & deleter) const override;
  virtual Storage storageWithAllocator(int64_t size, Allocator* allocator) const override;
  virtual std::unique_ptr<Generator> generator() const override;
  virtual const char * toString() const override;
  virtual size_t elementSizeInBytes() const override;
  virtual TypeID ID() const override;
  static const char * typeString();
  virtual Storage unsafeStorageFromTH(void * th_pointer, bool retain) const override;
  virtual Tensor unsafeTensorFromTH(void * th_pointer, bool retain) const override;

  // example
  // virtual Tensor * add(Tensor & a, Tensor & b) override;

  virtual Tensor & s_copy_(Tensor & self, const Tensor & src, bool non_blocking) const override;
  virtual Tensor & _s_copy_from(const Tensor & self, Tensor & dst, bool non_blocking) const override;
  ${type_derived_method_declarations}
};

} // namespace at
