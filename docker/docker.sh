#!/bin/bash
set -e

PLATFORM="${1:-rv1126}"
shift
BUILD_ARGS="$@"

if ! command -v docker >/dev/null 2>&1; then
  echo "❌ Docker is not installed or not found in PATH!"
  echo "   Please install Docker: https://docs.docker.com/get-docker/"
  exit 1
fi

DOCKERFILE="Dockerfile.$PLATFORM"
IMAGE_NAME="image-$PLATFORM"
CONTAINER_NAME="container-$PLATFORM"

if [ ! -f "$DOCKERFILE" ]; then
  echo "❌ Dockerfile '$DOCKERFILE' not found!"
  echo "   Available options:"
  ls Dockerfile.* | sed 's/^/   • /'
  exit 1
fi

echo "🔍 Checking Docker image $IMAGE_NAME..."
# Build image if it doesn't exist
if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
  echo "📦 Docker image $IMAGE_NAME not found — building from $DOCKERFILE..."
  docker buildx build --platform linux/amd64 -f "$DOCKERFILE" -t "$IMAGE_NAME" .
fi

echo "🔍 Checking container $CONTAINER_NAME status..."
CONTAINER_ID=$(docker ps -aqf "name=^${CONTAINER_NAME}$")

if [ -n "$CONTAINER_ID" ]; then
  STATUS=$(docker inspect -f '{{.State.Status}}' "$CONTAINER_ID")
  if [ "$STATUS" != "running" ]; then
    echo "❗ Container $CONTAINER_NAME is in status '$STATUS' — removing..."
    docker rm -f "$CONTAINER_ID" >/dev/null
    CONTAINER_ID=""
  else
    echo "✅ Container $CONTAINER_NAME is already running."
  fi
fi

# Create container if it doesn't exist
if [ -z "$CONTAINER_ID" ]; then
  echo "🚀 Creating container $CONTAINER_NAME..."
  docker run --init --privileged -dit \
    --platform=linux/amd64 \
    --name "$CONTAINER_NAME" \
    -v "$PWD:/workspace" \
    -w /workspace \
    "$IMAGE_NAME"
fi

# Run bash if called with arguments
if [ -n "$BUILD_ARGS" ]; then
  echo "🏗️  Running inside container: $BUILD_ARGS"
  docker exec -it "$CONTAINER_NAME" bash -c "$BUILD_ARGS"
else
  echo "ℹ️  No arguments provided."
fi

# Stop container after build
echo "🛑 Stopping container $CONTAINER_NAME..."
docker stop "$CONTAINER_NAME" >/dev/null
