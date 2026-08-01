import json

import boto3


def _event(sub, method, body=None):
    event = {
        "requestContext": {
            "authorizer": {"jwt": {"claims": {"sub": sub}}} if sub else {},
            "http": {"method": method},
        },
    }
    if body is not None:
        event["body"] = body
    return event


def _get_object(bucket, key):
    return boto3.client("s3").get_object(Bucket=bucket, Key=key)["Body"].read().decode()


def test_handler_requires_sub(labels):
    event = _event(None, "GET")
    resp = labels.handler(event, None)
    assert resp["statusCode"] == 401


def test_get_returns_empty_when_no_labels_saved(labels):
    resp = labels.handler(_event("user-1", "GET"), None)
    assert resp["statusCode"] == 200
    assert json.loads(resp["body"]) == {}


def test_put_then_get_round_trips(labels):
    put_resp = labels.handler(_event("user-1", "PUT", body=json.dumps({"dev1": "リビング"})), None)
    assert put_resp["statusCode"] == 200
    assert json.loads(put_resp["body"]) == {"ok": True}

    get_resp = labels.handler(_event("user-1", "GET"), None)
    assert json.loads(get_resp["body"]) == {"dev1": "リビング"}


def test_put_rejects_invalid_json(labels):
    resp = labels.handler(_event("user-1", "PUT", body="not json"), None)
    assert resp["statusCode"] == 400


def test_put_defaults_to_empty_object_when_body_missing(labels):
    resp = labels.handler(_event("user-1", "PUT"), None)
    assert resp["statusCode"] == 200
    assert _get_object(labels.S3_BUCKET, "labels/user-1.json") == "{}"


def test_unsupported_method_returns_405(labels):
    resp = labels.handler(_event("user-1", "DELETE"), None)
    assert resp["statusCode"] == 405


def test_labels_are_isolated_per_user(labels):
    labels.handler(_event("user-1", "PUT", body=json.dumps({"dev1": "user1のラベル"})), None)
    labels.handler(_event("user-2", "PUT", body=json.dumps({"dev1": "user2のラベル"})), None)

    resp1 = labels.handler(_event("user-1", "GET"), None)
    resp2 = labels.handler(_event("user-2", "GET"), None)

    assert json.loads(resp1["body"]) == {"dev1": "user1のラベル"}
    assert json.loads(resp2["body"]) == {"dev1": "user2のラベル"}
