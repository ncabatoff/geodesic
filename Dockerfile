FROM cloudposse/geodesic:0.13.0

ENV DOCKER_IMAGE "cloudposse/dev.yttrium.cc"
ENV DOCKER_TAG "dev"

ENV AWS_DEFAULT_PROFILE "dev"

ENV AWS_REGION "us-east-1"

ENV TF_BUCKET         yttrium-dev-terraform-state
ENV TF_DYNAMODB_TABLE yttrium-dev-terraform-state-lock

ENV TF_VAR_tfstate_stage     dev
ENV TF_VAR_tfstate_namespace yttrium
ENV TF_VAR_tfstate_region    us-east-1

RUN git config --global credential.helper "!aws codecommit credential-helper $@"
RUN git config --global credential.UseHttpPath true
RUN git config --global user.name "Nick Cabatoff"
RUN git config --global user.email "nick@yttrium.cc"

COPY bin/ /root/

WORKDIR /root
