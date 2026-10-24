#include <string>

#include "minikv/util/status.h"

#include "support/test_framework.h"

TEST(Status, OkIsOk) {
  const minikv::Status status;
  CHECK_TRUE(status.ok());
  CHECK_EQ(status.code(), minikv::StatusCode::kOk);
  CHECK_STREQ(status.ToString(), "OK");
  CHECK_TRUE(status.message().empty());
}

TEST(Status, NotFoundKeepsMessage) {
  const minikv::Status status = minikv::Status::NotFound("user:404");
  CHECK_FALSE(status.ok());
  CHECK_TRUE(status.IsNotFound());
  CHECK_EQ(status.code(), minikv::StatusCode::kNotFound);
  CHECK_STREQ(status.ToString(), "NotFound: user:404");
  CHECK_STREQ(status.message(), "user:404");
}

TEST(Status, EmptyMessageOmitsSeparator) {
  const minikv::Status status = minikv::Status::IOError("");
  CHECK_STREQ(status.ToString(), "IOError");
}

TEST(Status, AllFactoriesSetTheirCode) {
  CHECK_EQ(minikv::Status::IOError("x").code(), minikv::StatusCode::kIOError);
  CHECK_EQ(minikv::Status::Corruption("x").code(), minikv::StatusCode::kCorruption);
  CHECK_EQ(minikv::Status::InvalidArgument("x").code(), minikv::StatusCode::kInvalidArgument);
  CHECK_EQ(minikv::Status::NotSupported("x").code(), minikv::StatusCode::kNotSupported);
  CHECK_EQ(minikv::Status::Closed("x").code(), minikv::StatusCode::kClosed);
}

TEST(Status, IsNotFoundIsSpecific) {
  CHECK_FALSE(minikv::Status::IOError("io").IsNotFound());
  CHECK_FALSE(minikv::Status().IsNotFound());
}
